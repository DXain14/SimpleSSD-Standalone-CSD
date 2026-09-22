/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

#include "sil/nvme/nvme.hh"
#include "sim/engine.hh"
#include "simplessd/util/simplessd.hh"

namespace {

const uint32_t kQueueDepth = 32;
const uint64_t kControlBytes = 256;
const uint64_t kVectorOffset = 56;
const uint64_t kOutputOffset = kVectorOffset + 16 * 2;

void store16(std::vector<uint8_t> &buffer, uint64_t offset, uint16_t value) {
  buffer[offset + 0] = value & 0xFF;
  buffer[offset + 1] = (value >> 8) & 0xFF;
}

void store32(std::vector<uint8_t> &buffer, uint64_t offset, uint32_t value) {
  buffer[offset + 0] = value & 0xFF;
  buffer[offset + 1] = (value >> 8) & 0xFF;
  buffer[offset + 2] = (value >> 16) & 0xFF;
  buffer[offset + 3] = (value >> 24) & 0xFF;
}

void store64(std::vector<uint8_t> &buffer, uint64_t offset, uint64_t value) {
  store32(buffer, offset, value & 0xFFFFFFFF);
  store32(buffer, offset + 4, value >> 32);
}

uint16_t load16(const std::vector<uint8_t> &buffer, uint64_t offset) {
  return (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
}

uint32_t load32(const std::vector<uint8_t> &buffer, uint64_t offset) {
  return (uint32_t)buffer[offset] | ((uint32_t)buffer[offset + 1] << 8) |
         ((uint32_t)buffer[offset + 2] << 16) |
         ((uint32_t)buffer[offset + 3] << 24);
}

float loadFloat(const std::vector<uint8_t> &buffer, uint64_t offset) {
  uint32_t raw = load32(buffer, offset);
  float value;

  memcpy(&value, &raw, sizeof(value));

  return value;
}

float decodeFP16(uint16_t value) {
  uint32_t sign = (value >> 15) & 0x1;
  uint32_t exponent = (value >> 10) & 0x1F;
  uint32_t fraction = value & 0x03FF;
  float ret;

  if (exponent == 0) {
    ret = fraction == 0 ? 0.f : std::ldexp((float)fraction, -24);
  }
  else if (exponent == 0x1F) {
    ret = fraction == 0 ? std::numeric_limits<float>::infinity()
                        : std::numeric_limits<float>::quiet_NaN();
  }
  else {
    ret = std::ldexp(1.f + (float)fraction / 1024.f, (int)exponent - 15);
  }

  return sign ? -ret : ret;
}

uint16_t halfPattern(uint64_t index) {
  static const uint16_t values[] = {
      0x0000, 0x3800, 0x3C00, 0x4000, 0xBC00, 0xC000,
  };

  return values[index % (sizeof(values) / sizeof(values[0]))];
}

std::vector<uint8_t> makeMatrix() {
  std::vector<uint8_t> matrix(16 * 16 * 2, 0);

  for (uint64_t i = 0; i < 16 * 16; i++) {
    store16(matrix, i * 2, halfPattern(i + 7));
  }

  return matrix;
}

std::vector<uint16_t> makeVector(uint64_t seed) {
  std::vector<uint16_t> vector(16);

  for (uint32_t i = 0; i < vector.size(); i++) {
    vector[i] = halfPattern(seed + i * 5);
  }

  return vector;
}

std::vector<float> referenceGEMV(const std::vector<uint8_t> &matrix,
                                 const std::vector<uint16_t> &vector) {
  std::vector<float> result(16, 0.f);

  for (uint32_t row = 0; row < 16; row++) {
    for (uint32_t col = 0; col < 16; col++) {
      float a = decodeFP16(load16(matrix, ((uint64_t)row * 16 + col) * 2));
      float x = decodeFP16(vector[col]);

      result[row] += a * x;
    }
  }

  return result;
}

std::vector<uint8_t> makeControl(uint64_t matrixSLBA,
                                 const std::vector<uint16_t> &vector) {
  std::vector<uint8_t> control(kControlBytes, 0);

  control[0] = 'C';
  control[1] = 'S';
  control[2] = 'D';
  control[3] = '0';
  store16(control, 4, 1);
  store16(control, 6, 1);
  store32(control, 8, 0);
  store64(control, 16, matrixSLBA);
  store32(control, 24, 16);
  store32(control, 28, 16);
  store32(control, 32, 16);
  store64(control, 40, kVectorOffset);
  store64(control, 48, kOutputOffset);

  for (uint32_t i = 0; i < vector.size(); i++) {
    store16(control, kVectorOffset + i * 2, vector[i]);
  }

  return control;
}

struct WorkItem {
  std::vector<uint8_t> control;
  std::vector<uint8_t> readBuffer;
  std::vector<uint8_t> writeBuffer;
  std::vector<float> expected;
};

class CSDStress {
 private:
  Engine &engine;
  SimpleSSD::ConfigReader &config;
  SIL::NVMe::Driver driver;
  uint64_t operationCount;
  bool requirePayloadCopies;
  uint64_t submitted;
  uint64_t completed;
  uint32_t outstanding;
  uint32_t lbaSize;
  uint64_t capacity;
  uint64_t matrixOffset;
  uint64_t readOffset;
  uint64_t writeOffset;
  bool finished;
  int result;
  std::vector<uint8_t> matrix;
  std::vector<uint8_t> stableRead;
  std::vector<WorkItem> work;
  std::queue<uint32_t> freeSlots;
  std::vector<SimpleSSD::Stats> statNames;
  std::vector<double> statBase;

  void fail(const std::string &message) {
    if (!finished) {
      std::cerr << message << std::endl;
      result = 1;
      finished = true;
      engine.stopEngine();
    }
  }

  double statValue(const std::vector<double> &values,
                   const std::string &name) const {
    for (size_t i = 0; i < statNames.size(); i++) {
      if (statNames[i].name == name) {
        return values[i];
      }
    }

    return -1.;
  }

  bool checkOutput(const WorkItem &item) const {
    for (uint32_t i = 0; i < item.expected.size(); i++) {
      if (std::fabs(loadFloat(item.control, kOutputOffset + (uint64_t)i * 4) -
                    item.expected[i]) > 0.001f) {
        return false;
      }
    }

    return true;
  }

  void prepare() {
    driver.getInfo(capacity, lbaSize);
    driver.initStats(statNames);

    if (lbaSize != 512 || capacity < 512 * 1024 || operationCount % 10 != 0) {
      fail("stress test requires a 512-byte LBA namespace and a multiple of 10 operations");

      return;
    }

    matrixOffset = 0;
    readOffset = lbaSize;
    writeOffset = 8 * lbaSize;
    matrix = makeMatrix();
    stableRead.assign(lbaSize, 0x5A);

    driver.submitWriteBuffer(
        matrixOffset, matrix.data(), matrix.size(), [this](uint16_t status) {
          if (status != 0) {
            fail("stress precondition matrix write failed");
          }
          else {
            writeStableRead();
          }
        });
  }

  void writeStableRead() {
    driver.submitWriteBuffer(
        readOffset, stableRead.data(), stableRead.size(),
        [this](uint16_t status) {
          if (status != 0) {
            fail("stress precondition read target write failed");
          }
          else {
            startWorkload();
          }
        });
  }

  void startWorkload() {
    driver.getStats(statBase);
    work.resize(kQueueDepth);
    for (uint32_t i = 0; i < kQueueDepth; i++) {
      freeSlots.push(i);
    }
    submitMore();
  }

  void submitMore() {
    while (!finished && submitted < operationCount && !freeSlots.empty()) {
      uint32_t slot = freeSlots.front();
      uint64_t operation = submitted++;
      uint32_t selector = operation % 10;

      freeSlots.pop();
      outstanding++;

      if (selector < 7) {
        submitReadCompute(slot, operation);
      }
      else if (selector < 9) {
        submitRead(slot);
      }
      else {
        submitWrite(slot, operation);
      }
    }
  }

  void submitReadCompute(uint32_t slot, uint64_t operation) {
    WorkItem &item = work[slot];
    std::vector<uint16_t> vector = makeVector(operation);

    item.control = makeControl(matrixOffset / lbaSize, vector);
    item.expected = referenceGEMV(matrix, vector);
    driver.submitCSDReadCompute(
        item.control, [this, slot](uint16_t status) {
          if (status != 0 || !checkOutput(work[slot])) {
            fail("stress read_compute returned an incorrect completion or GEMV");
          }
          else {
            complete(slot);
          }
        });
  }

  void submitRead(uint32_t slot) {
    WorkItem &item = work[slot];

    item.readBuffer.assign(lbaSize, 0);
    driver.submitReadBuffer(
        readOffset, item.readBuffer, [this, slot](uint16_t status) {
          if (status != 0 || work[slot].readBuffer != stableRead) {
            fail("stress ordinary read lost data integrity");
          }
          else {
            complete(slot);
          }
        });
  }

  void submitWrite(uint32_t slot, uint64_t operation) {
    WorkItem &item = work[slot];
    uint64_t slotOffset = writeOffset + ((operation / 10) % 512) * lbaSize;

    item.writeBuffer.assign(lbaSize, (uint8_t)(operation & 0xFF));
    driver.submitWriteBuffer(
        slotOffset, item.writeBuffer.data(), item.writeBuffer.size(),
        [this, slot](uint16_t status) {
          if (status != 0) {
            fail("stress ordinary write failed");
          }
          else {
            complete(slot);
          }
        });
  }

  void complete(uint32_t slot) {
    if (finished) {
      return;
    }

    completed++;
    outstanding--;
    freeSlots.push(slot);

    if (completed == operationCount) {
      verifyStatistics();
    }
    else {
      submitMore();
    }
  }

  void verifyStatistics() {
    std::vector<double> values;
    uint64_t expectedCSD = operationCount / 10 * 7;
    double commandCount;
    double failedCount;
    double queueLatency;
    double gcCount;
    double reclaimedBlocks;
    double ftlPageCopies;
    double arrayCopies;
    double arrayErases;
    double palReads;

    driver.getStats(values);
    commandCount = statValue(values, "csd.pu.command.count") -
                   statValue(statBase, "csd.pu.command.count");
    failedCount = statValue(values, "csd.pu.command.failed") -
                  statValue(statBase, "csd.pu.command.failed");
    queueLatency = statValue(values, "csd.pu.latency.compute_queue") -
                   statValue(statBase, "csd.pu.latency.compute_queue");
    gcCount = statValue(values, "ftl.page_mapping.gc.count") -
              statValue(statBase, "ftl.page_mapping.gc.count");
    reclaimedBlocks =
        statValue(values, "ftl.page_mapping.gc.reclaimed_blocks") -
        statValue(statBase, "ftl.page_mapping.gc.reclaimed_blocks");
    ftlPageCopies = statValue(values, "ftl.page_mapping.gc.page_copies") -
                    statValue(statBase, "ftl.page_mapping.gc.page_copies");
    arrayCopies = statValue(values, "csd.array.copy.count") -
                  statValue(statBase, "csd.array.copy.count");
    arrayErases = statValue(values, "csd.array.erase.count") -
                  statValue(statBase, "csd.array.erase.count");
    palReads = statValue(values, "pal.read.count") -
               statValue(statBase, "pal.read.count");

    if (outstanding != 0 || completed != operationCount ||
        submitted != operationCount || commandCount != expectedCSD ||
        failedCount != 0 || queueLatency <= 0 || gcCount <= 0 ||
        arrayErases != reclaimedBlocks || arrayCopies != ftlPageCopies ||
        (requirePayloadCopies && arrayCopies <= 0) ||
        palReads < expectedCSD) {
      std::cerr << "stress stat snapshot: commands=" << commandCount
                << " failed=" << failedCount << " queue=" << queueLatency
                << " gc=" << gcCount << " reclaimed=" << reclaimedBlocks
                << " ftl_page_copies=" << ftlPageCopies
                << " array_copies=" << arrayCopies
                << " array_erases=" << arrayErases
                << " pal_reads=" << palReads << std::endl;
      fail("stress statistics do not show serialized compute and GC migration");

      return;
    }

    std::cout << "CSD stress passed: " << operationCount
              << " commands at QD " << kQueueDepth << std::endl;
    result = 0;
    finished = true;
    engine.stopEngine();
  }

 public:
  CSDStress(Engine &e, SimpleSSD::ConfigReader &c, uint64_t operations,
            bool requireCopies)
      : engine(e),
        config(c),
        driver(engine, config),
        operationCount(operations),
        requirePayloadCopies(requireCopies),
        submitted(0),
        completed(0),
        outstanding(0),
        lbaSize(0),
        capacity(0),
        matrixOffset(0),
        readOffset(0),
        writeOffset(0),
        finished(false),
        result(1) {}

  int run() {
    std::function<void()> start = [this]() { prepare(); };

    driver.init(start);
    while (engine.doNextEvent())
      ;

    return finished ? result : 1;
  }
};

}  // namespace

int main(int argc, char **argv) {
  uint64_t operations = 100000;
  bool requirePayloadCopies = false;

  if (argc < 2 || argc > 4) {
    std::cerr << "Usage: simplessd-csd-stress <SimpleSSD config file> "
                 "[operation count] [--require-payload-copy]"
              << std::endl;
    return 1;
  }

  if (argc == 3) {
    char *end = nullptr;

    operations = strtoull(argv[2], &end, 10);
    if (!end || *end != '\0' || operations == 0 || operations % 10 != 0) {
      std::cerr << "operation count should be a positive multiple of 10"
                << std::endl;
      return 1;
    }
  }
  else if (argc == 4) {
    char *end = nullptr;

    operations = strtoull(argv[2], &end, 10);
    if (!end || *end != '\0' || operations == 0 || operations % 10 != 0 ||
        strcmp(argv[3], "--require-payload-copy") != 0) {
      std::cerr << "operation count should be a positive multiple of 10 and "
                   "the optional flag should be --require-payload-copy"
                << std::endl;
      return 1;
    }

    requirePayloadCopies = true;
  }

  Engine engine;
  auto config = initSimpleSSDEngine(&engine, nullptr, nullptr, argv[1]);
  CSDStress test(engine, config, operations, requirePayloadCopies);
  int result = test.run();

  releaseSimpleSSDEngine();

  return result;
}
