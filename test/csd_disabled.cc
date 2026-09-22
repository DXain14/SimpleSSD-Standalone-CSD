/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <cstring>
#include <iostream>
#include <vector>

#include "sil/nvme/nvme.hh"
#include "sim/engine.hh"
#include "simplessd/hil/nvme/def.hh"
#include "simplessd/util/simplessd.hh"

namespace {

bool hasStatus(uint16_t status, SimpleSSD::HIL::NVMe::STATUS_CODE_TYPE type,
               int code) {
  uint16_t expected =
      ((uint16_t)(type & 0x07) << 8) | (uint16_t)(code & 0xFF);

  return (status & 0x07FF) == expected;
}

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

class CSDDisabledTest {
 private:
  Engine &engine;
  SimpleSSD::ConfigReader &config;
  SIL::NVMe::Driver driver;
  std::vector<uint8_t> writeBuffer;
  std::vector<uint8_t> readBuffer;
  bool finished;
  int result;

  void fail(const char *message) {
    if (!finished) {
      std::cerr << message << std::endl;
      finished = true;
      result = 1;
      engine.stopEngine();
    }
  }

  void begin() {
    uint64_t capacity = 0;
    uint32_t lbaSize = 0;

    driver.getInfo(capacity, lbaSize);
    if (capacity < 512 || lbaSize != 512) {
      fail("unexpected disabled-CSD namespace geometry");

      return;
    }

    writeBuffer.assign(lbaSize, 0xA5);
    readBuffer.assign(lbaSize, 0);
    driver.submitWriteBuffer(0, writeBuffer.data(), writeBuffer.size(),
                             [this](uint16_t status) {
      if (status != 0) {
        fail("ordinary write failed with CSD disabled");
      }
      else {
        verifyRead();
      }
    });
  }

  void verifyRead() {
    driver.submitReadBuffer(0, readBuffer, [this](uint16_t status) {
      if (status != 0 || readBuffer != writeBuffer) {
        fail("ordinary read regressed with CSD disabled");
      }
      else {
        verifyCompare();
      }
    });
  }

  void verifyCompare() {
    driver.submitCompareBuffer(
        0, writeBuffer.data(), writeBuffer.size(), [this](uint16_t status) {
          if (status != 0) {
            fail("ordinary compare regressed with CSD disabled");
          }
          else {
            verifyReadComputeRejected();
          }
        });
  }

  void verifyReadComputeRejected() {
    std::vector<uint8_t> control(128, 0);

    control[0] = 'C';
    control[1] = 'S';
    control[2] = 'D';
    control[3] = '0';
    store16(control, 4, 1);
    store16(control, 6, 1);
    store64(control, 16, 0);
    store32(control, 24, 1);
    store32(control, 28, 1);
    store32(control, 32, 1);
    store64(control, 40, 56);
    store64(control, 48, 80);
    store16(control, 56, 0x3C00);

    driver.submitCSDReadCompute(
        control, [this](uint16_t status) {
          if (!hasStatus(status,
                         SimpleSSD::HIL::NVMe::TYPE_GENERIC_COMMAND_STATUS,
                         SimpleSSD::HIL::NVMe::STATUS_INVALID_OPCODE)) {
            fail("read_compute did not reject CSD-disabled configuration");
          }
          else {
            std::cout << "CSD disabled ordinary I/O regression passed"
                      << std::endl;
            finished = true;
            result = 0;
            engine.stopEngine();
          }
        });
  }

 public:
  CSDDisabledTest(Engine &e, SimpleSSD::ConfigReader &c)
      : engine(e), config(c), driver(engine, config), finished(false), result(1) {}

  int run() {
    std::function<void()> start = [this]() { begin(); };

    driver.init(start);
    while (engine.doNextEvent())
      ;

    return finished ? result : 1;
  }
};

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: simplessd-csd-disabled <SimpleSSD config file>"
              << std::endl;
    return 1;
  }

  Engine engine;
  auto config = initSimpleSSDEngine(&engine, nullptr, nullptr, argv[1]);
  CSDDisabledTest test(engine, config);
  int result = test.run();

  releaseSimpleSSDEngine();

  return result;
}
