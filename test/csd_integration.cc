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

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "sil/nvme/nvme.hh"
#include "sim/engine.hh"
#include "simplessd/csd/config.hh"
#include "simplessd/hil/nvme/def.hh"
#include "simplessd/util/simplessd.hh"

namespace {

const uint64_t kDescriptorBytes = 56;

bool hasStatus(uint16_t status, SimpleSSD::HIL::NVMe::STATUS_CODE_TYPE type,
               int code) {
  uint16_t expected =
      ((uint16_t)(type & 0x07) << 8) | (uint16_t)(code & 0xFF);

  // The standalone Driver strips the CQ phase bit before invoking callbacks.
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

std::vector<uint8_t> makeMatrix(uint32_t rows, uint32_t cols,
                                uint64_t transferBytes, uint64_t seed) {
  std::vector<uint8_t> matrix(transferBytes, 0);

  for (uint64_t i = 0; i < (uint64_t)rows * cols; i++) {
    store16(matrix, i * 2, halfPattern(i + seed));
  }

  return matrix;
}

std::vector<uint16_t> makeVector(uint32_t cols, uint64_t seed) {
  std::vector<uint16_t> vector(cols);

  for (uint32_t i = 0; i < cols; i++) {
    vector[i] = halfPattern(i * 3 + seed);
  }

  return vector;
}

std::vector<float> referenceGEMV(const std::vector<uint8_t> &matrix,
                                 uint32_t rows, uint32_t cols,
                                 const std::vector<uint16_t> &vector) {
  std::vector<float> result(rows, 0.f);

  for (uint32_t row = 0; row < rows; row++) {
    for (uint32_t col = 0; col < cols; col++) {
      float a = decodeFP16(load16(matrix, ((uint64_t)row * cols + col) * 2));
      float x = decodeFP16(vector[col]);

      result[row] += a * x;
    }
  }

  return result;
}

std::vector<uint8_t> makeControl(uint64_t matrixSLBA, uint32_t rows,
                                 uint32_t cols,
                                 const std::vector<uint16_t> &vector,
                                 uint64_t controlBytes,
                                 uint64_t vectorOffset,
                                 uint64_t outputOffset) {
  std::vector<uint8_t> control(controlBytes, 0);

  control[0] = 'C';
  control[1] = 'S';
  control[2] = 'D';
  control[3] = '0';
  store16(control, 4, 1);
  store16(control, 6, 1);
  store32(control, 8, 0);
  store64(control, 16, matrixSLBA);
  store32(control, 24, rows);
  store32(control, 28, cols);
  store32(control, 32, cols);
  store64(control, 40, vectorOffset);
  store64(control, 48, outputOffset);

  for (uint32_t i = 0; i < cols; i++) {
    store16(control, vectorOffset + i * 2, vector[i]);
  }

  return control;
}

class CSDIntegration {
 private:
  Engine &engine;
  SimpleSSD::ConfigReader &config;
  SIL::NVMe::Driver driver;
  uint8_t opcode;
  uint64_t capacity;
  uint32_t lbaSize;
  bool finished;
  int result;
  std::vector<SimpleSSD::Stats> statNames;

  std::vector<uint8_t> smallMatrix;
  std::vector<uint8_t> smallReadback;
  std::vector<uint16_t> smallVector;
  std::vector<float> smallExpected;
  std::vector<uint8_t> smallControl;
  uint64_t smallOffset;

  std::vector<uint8_t> largeMatrix;
  std::vector<uint16_t> largeVector;
  std::vector<float> largeExpected;
  std::vector<uint8_t> largeControl;
  std::vector<uint8_t> largeReadback;
  uint64_t largeOffset;

  void fail(const std::string &message) {
    if (!finished) {
      std::cerr << message << std::endl;
      result = 1;
      finished = true;
      engine.stopEngine();
    }
  }

  void succeed() {
    if (!finished) {
      std::cout << "CSD integration tests passed" << std::endl;
      result = 0;
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

  bool checkOutput(const std::vector<uint8_t> &control, uint64_t outputOffset,
                   const std::vector<float> &expected) {
    for (uint32_t i = 0; i < expected.size(); i++) {
      float actual = loadFloat(control, outputOffset + (uint64_t)i * 4);

      if (std::isnan(expected[i])) {
        if (!std::isnan(actual)) {
          return false;
        }
      }
      else if (std::isinf(expected[i])) {
        if (!std::isinf(actual) ||
            std::signbit(actual) != std::signbit(expected[i])) {
          return false;
        }
      }
      else if (std::fabs(actual - expected[i]) >
               std::max(0.000001f * std::fabs(expected[i]), 0.000000001f)) {
        return false;
      }
    }

    return true;
  }

  bool checkControlInputPreserved(const std::vector<uint8_t> &before,
                                  const std::vector<uint8_t> &after,
                                  uint64_t outputOffset,
                                  uint64_t outputBytes) const {
    if (before.size() != after.size()) {
      return false;
    }

    for (uint64_t i = 0; i < before.size(); i++) {
      if (i < outputOffset || i >= outputOffset + outputBytes) {
        if (before[i] != after[i]) {
          return false;
        }
      }
    }

    return true;
  }

  void printOutputMismatch(const std::vector<uint8_t> &control,
                           uint64_t outputOffset,
                           const std::vector<float> &expected) const {
    for (uint32_t i = 0; i < expected.size(); i++) {
      float actual = loadFloat(control, outputOffset + (uint64_t)i * 4);

      if ((std::isnan(expected[i]) && !std::isnan(actual)) ||
          (std::isinf(expected[i]) &&
           (!std::isinf(actual) ||
            std::signbit(actual) != std::signbit(expected[i]))) ||
          (!std::isnan(expected[i]) && !std::isinf(expected[i]) &&
           std::fabs(actual - expected[i]) >
               std::max(0.000001f * std::fabs(expected[i]), 0.000000001f))) {
        std::cerr << "GEMV mismatch at row " << i << ": expected "
                  << expected[i] << ", got " << actual << std::endl;
        return;
      }
    }
  }

  void printControlInputMismatch(const std::vector<uint8_t> &before,
                                 const std::vector<uint8_t> &after,
                                 uint64_t outputOffset,
                                 uint64_t outputBytes) const {
    for (uint64_t i = 0; i < before.size(); i++) {
      if ((i < outputOffset || i >= outputOffset + outputBytes) &&
          before[i] != after[i]) {
        std::cerr << "control input changed at byte " << i << ": expected "
                  << (uint32_t)before[i] << ", got " << (uint32_t)after[i]
                  << std::endl;
        return;
      }
    }
  }

  void begin() {
    driver.getInfo(capacity, lbaSize);
    driver.initStats(statNames);

    if (lbaSize != 512 || capacity < 128 * 1024) {
      fail("unexpected standalone test namespace geometry");

      return;
    }

    smallOffset = 0;
    smallMatrix = makeMatrix(3, 5, lbaSize, 1);
    smallReadback.assign(lbaSize, 0);
    smallVector = makeVector(5, 3);
    smallExpected = referenceGEMV(smallMatrix, 3, 5, smallVector);
    smallControl =
        makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);

    largeOffset = 16 * lbaSize;
    largeMatrix = makeMatrix(128, 128, 128 * 128 * 2, 11);
    largeVector = makeVector(128, 17);
    largeExpected = referenceGEMV(largeMatrix, 128, 128, largeVector);
    largeControl =
        makeControl(largeOffset / lbaSize, 128, 128, largeVector, 12288,
                    8192, 8192 + 128 * 2);
    largeReadback.assign(largeMatrix.size(), 0);

    driver.submitWriteBuffer(smallOffset, smallMatrix.data(), smallMatrix.size(),
                             [this](uint16_t status) {
      if (status != 0) {
        fail("ordinary NVMe write for the small matrix failed");
      }
      else {
        verifyOrdinaryRead();
      }
    });
  }

  void verifyOrdinaryRead() {
    driver.submitReadBuffer(
        smallOffset, smallReadback, [this](uint16_t status) {
          if (status != 0 || smallReadback != smallMatrix) {
            fail("ordinary NVMe read did not return the written matrix");
          }
          else {
            verifyOrdinaryCompare();
          }
        });
  }

  void verifyOrdinaryCompare() {
    driver.submitCompareBuffer(
        smallOffset, smallMatrix.data(), smallMatrix.size(),
        [this](uint16_t status) {
          if (status != 0) {
            fail("ordinary NVMe compare did not see the written matrix");
          }
          else {
            verifyScalarGEMV();
          }
        });
  }

  void verifyScalarGEMV() {
    auto matrix = std::make_shared<std::vector<uint8_t>>(lbaSize, 0);
    auto vector = std::make_shared<std::vector<uint16_t>>(1, 0x3C00);
    auto control = std::make_shared<std::vector<uint8_t>>(
        makeControl(4, 1, 1, *vector, 128, kDescriptorBytes, 80));
    auto expected = std::make_shared<std::vector<float>>(1, 2.f);

    store16(*matrix, 0, 0x4000);
    driver.submitWriteBuffer(
        4 * lbaSize, matrix->data(), matrix->size(),
        [this, matrix, vector, control, expected](uint16_t status) {
          if (status != 0) {
            fail("ordinary NVMe write for 1x1 GEMV failed");
          }
          else {
            driver.submitCSDReadCompute(
                *control, opcode, false,
                [this, control, expected](uint16_t computeStatus) {
                  if (computeStatus != 0 ||
                      !checkOutput(*control, 80, *expected)) {
                    fail("1x1 read_compute returned an incorrect GEMV result");
                  }
                  else {
                    verifySpecialGEMV();
                  }
                });
          }
        });
  }

  void verifySpecialGEMV() {
    auto matrix = std::make_shared<std::vector<uint8_t>>(lbaSize, 0);
    auto vector = std::make_shared<std::vector<uint16_t>>(
        std::vector<uint16_t>{0x3C00, 0x3C00, 0x3C00});
    auto control = std::make_shared<std::vector<uint8_t>>(
        makeControl(5, 3, 3, *vector, 128, kDescriptorBytes, 80));
    auto expected = std::make_shared<std::vector<float>>(
        std::vector<float>{decodeFP16(0x0001),
                           std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()});

    store16(*matrix, 0, 0x0001);
    store16(*matrix, 8, 0x7C00);
    store16(*matrix, 16, 0x7E00);
    driver.submitWriteBuffer(
        5 * lbaSize, matrix->data(), matrix->size(),
        [this, matrix, vector, control, expected](uint16_t status) {
          if (status != 0) {
            fail("ordinary NVMe write for special FP16 GEMV failed");
          }
          else {
            driver.submitCSDReadCompute(
                *control, opcode, false,
                [this, control, expected](uint16_t computeStatus) {
                  if (computeStatus != 0 ||
                      !checkOutput(*control, 80, *expected)) {
                    fail("read_compute did not preserve FP16 special values");
                  }
                  else {
                    verifyMeasuredReadCompute();
                  }
                });
          }
        });
  }

  void verifyMeasuredReadCompute() {
    std::vector<double> before;
    auto original = std::make_shared<std::vector<uint8_t>>(smallControl);

    driver.getStats(before);

    driver.submitCSDReadCompute(
        smallControl, opcode, false, [this, before, original](uint16_t status) {
          std::vector<double> after;
          uint64_t matrixBytes = 3 * 5 * 2;
          uint64_t computeOps = 2 * 3 * 5;
          uint64_t expectedReadLatency =
              (uint64_t)std::ceil((double)matrixBytes * 1000000000000. /
                                  config.readUint(
                                      SimpleSSD::CONFIG_CSD,
                                      SimpleSSD::CSD::CSD_INTERNAL_READ_BANDWIDTH));
          uint64_t expectedComputeLatency =
              (uint64_t)std::ceil((double)computeOps * 1000000000000. /
                                  (config.readFloat(
                                       SimpleSSD::CONFIG_CSD,
                                       SimpleSSD::CSD::CSD_PU_COMPUTE_GFLOPS) *
                                   1000000000.));

          driver.getStats(after);

          if (status != 0 || !checkOutput(smallControl, 80, smallExpected) ||
              !checkControlInputPreserved(*original, smallControl, 80, 3 * 4)) {
            fail("PRP read_compute returned an incorrect GEMV result");
          }
          else if (statValue(after, "csd.pu.command.count") -
                       statValue(before, "csd.pu.command.count") !=
                   1 ||
                   statValue(after, "csd.pu.command.failed") -
                           statValue(before, "csd.pu.command.failed") !=
                       0 ||
                   statValue(after, "csd.pu.array.read.bytes") -
                           statValue(before, "csd.pu.array.read.bytes") !=
                       matrixBytes ||
                   statValue(after, "csd.pu.compute.ops") -
                           statValue(before, "csd.pu.compute.ops") !=
                       computeOps ||
                   statValue(after, "csd.pu.dma.in.bytes") -
                           statValue(before, "csd.pu.dma.in.bytes") !=
                       smallControl.size() ||
                   statValue(after, "csd.pu.dma.out.bytes") -
                           statValue(before, "csd.pu.dma.out.bytes") !=
                       3 * 4 ||
                   statValue(after, "csd.pu.latency.internal_read") -
                           statValue(before, "csd.pu.latency.internal_read") !=
                       expectedReadLatency ||
                   statValue(after, "csd.pu.latency.compute") -
                           statValue(before, "csd.pu.latency.compute") !=
                       expectedComputeLatency ||
                   statValue(after, "csd.pu.latency.compute_queue") -
                           statValue(before, "csd.pu.latency.compute_queue") !=
                       0 ||
                   statValue(after, "csd.array.read.bytes") -
                           statValue(before, "csd.array.read.bytes") !=
                       matrixBytes ||
                   statValue(after, "pal.read.count") -
                           statValue(before, "pal.read.count") < 1) {
            fail("read_compute statistics do not prove the expected data path");
          }
          else {
            uint64_t actualReadLatency =
                (uint64_t)(statValue(after, "csd.pu.latency.internal_read") -
                           statValue(before, "csd.pu.latency.internal_read"));
            uint64_t actualComputeLatency =
                (uint64_t)(statValue(after, "csd.pu.latency.compute") -
                           statValue(before, "csd.pu.latency.compute"));
            uint64_t actualTotalLatency =
                (uint64_t)(statValue(after, "csd.pu.latency.total") -
                           statValue(before, "csd.pu.latency.total"));

            std::cout << "CSD timing snapshot: internal_read="
                      << actualReadLatency << " compute="
                      << actualComputeLatency << " total=" << actualTotalLatency
                      << std::endl;
            verifyConcurrentCompletions();
          }
        });
  }

  void verifyConcurrentCompletions() {
    auto before = std::make_shared<std::vector<double>>();

    driver.getStats(*before);
    smallControl =
        makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    auto secondControl = std::make_shared<std::vector<uint8_t>>(smallControl);
    auto completed = std::make_shared<uint32_t>(0);

    auto complete = [this, before, completed, secondControl](uint16_t status) {
      if (status != 0) {
        fail("concurrent read_compute failed");

        return;
      }

      (*completed)++;
      if (*completed > 2) {
        fail("concurrent read_compute completed more than once");

        return;
      }

      if (*completed == 2) {
        std::vector<double> values;

        driver.getStats(values);
        if (statValue(values, "csd.pu.command.count") -
                    statValue(*before, "csd.pu.command.count") !=
                2 ||
            !checkOutput(smallControl, 80, smallExpected) ||
            !checkOutput(*secondControl, 80, smallExpected)) {
          fail("concurrent read_compute did not complete exactly twice");
        }
        else {
          writeLargeMatrix();
        }
      }
    };

    driver.submitCSDReadCompute(smallControl, opcode, false, complete);
    driver.submitCSDReadCompute(*secondControl, opcode, false, complete);
  }

  void writeLargeMatrix() {
    driver.submitWriteBuffer(
        largeOffset, largeMatrix.data(), largeMatrix.size(),
        [this](uint16_t status) {
          if (status != 0) {
            fail("ordinary NVMe write for cross-page matrix failed");
          }
          else {
            verifyLargeOrdinaryRead();
          }
        });
  }

  void verifyLargeOrdinaryRead() {
    driver.submitReadBuffer(
        largeOffset, largeReadback, [this](uint16_t status) {
          if (status != 0 || largeReadback != largeMatrix) {
            if (status == 0) {
              uint64_t offset = 0;

              while (offset < largeMatrix.size() &&
                     largeReadback[offset] == largeMatrix[offset]) {
                offset++;
              }

              if (offset < largeMatrix.size()) {
                std::cerr << "cross-page mismatch at byte " << offset
                          << ": expected "
                          << (uint32_t)largeMatrix[offset] << ", got "
                          << (uint32_t)largeReadback[offset] << std::endl;
              }
            }
            fail("ordinary cross-page NVMe read did not return written matrix");
          }
          else {
            verifyLargePRP();
          }
        });
  }

  void verifyLargePRP() {
    auto before = std::make_shared<std::vector<double>>();
    auto original = std::make_shared<std::vector<uint8_t>>(largeControl);

    driver.getStats(*before);
    driver.submitCSDReadCompute(
        largeControl, opcode, false, [this, before, original](uint16_t status) {
          std::vector<double> after;

          driver.getStats(after);
          if (status != 0) {
            fail("PRP-list read_compute returned an error completion");
          }
          else if (!checkOutput(largeControl, 8192 + 128 * 2,
                                largeExpected) ||
                   !checkControlInputPreserved(*original, largeControl,
                                               8192 + 128 * 2, 128 * 4)) {
            printOutputMismatch(largeControl, 8192 + 128 * 2, largeExpected);
            printControlInputMismatch(*original, largeControl, 8192 + 128 * 2,
                                      128 * 4);
            fail("PRP-list read_compute returned an incorrect GEMV result");
          }
          else if (statValue(after, "csd.pu.array.read.bytes") -
                           statValue(*before, "csd.pu.array.read.bytes") !=
                       largeMatrix.size() ||
                   statValue(after, "csd.pu.dma.in.bytes") -
                           statValue(*before, "csd.pu.dma.in.bytes") !=
                       largeControl.size() ||
                   statValue(after, "csd.pu.dma.in.bytes") -
                           statValue(*before, "csd.pu.dma.in.bytes") >=
                       largeMatrix.size()) {
            fail("PRP-list read_compute transferred matrix data from host");
          }
          else {
            verifyLargeSGL();
          }
        });
  }

  void verifyLargeSGL() {
    auto original = std::make_shared<std::vector<uint8_t>>(
        makeControl(largeOffset / lbaSize, 128, 128, largeVector, 12288,
                    8192, 8192 + 128 * 2));

    largeControl = *original;
    driver.submitCSDReadCompute(
        largeControl, opcode, true, [this, original](uint16_t status) {
          if (status != 0) {
            fail("SGL read_compute returned an error completion");
          }
          else if (!checkOutput(largeControl, 8192 + 128 * 2, largeExpected) ||
                   !checkControlInputPreserved(*original, largeControl,
                                               8192 + 128 * 2, 128 * 4)) {
            fail("SGL read_compute returned an incorrect GEMV result");
          }
          else {
            overwriteAndTrim();
          }
        });
  }

  void overwriteAndTrim() {
    smallMatrix = makeMatrix(3, 5, lbaSize, 29);
    smallExpected = referenceGEMV(smallMatrix, 3, 5, smallVector);
    smallControl =
        makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);

    driver.submitWriteBuffer(
        smallOffset, smallMatrix.data(), smallMatrix.size(),
        [this](uint16_t status) {
          if (status != 0) {
            fail("ordinary overwrite failed");
          }
          else {
            driver.submitCSDReadCompute(
                smallControl, opcode, false, [this](uint16_t computeStatus) {
                  if (computeStatus != 0 ||
                      !checkOutput(smallControl, 80, smallExpected)) {
                    fail("read_compute did not observe an ordinary overwrite");
                  }
                  else {
                    verifyInvalidDescriptors();
                  }
                });
          }
        });
  }

  void verifyInvalidDescriptors() {
    auto controls =
        std::make_shared<std::vector<std::vector<uint8_t>>>();
    std::vector<uint8_t> invalid =
        makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);

    invalid[0] = 'X';
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store16(invalid, 4, 2);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store16(invalid, 6, 2);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store32(invalid, 8, 1);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store32(invalid, 32, 4);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store64(invalid, 40, kDescriptorBytes - 2);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store64(invalid, 48, kDescriptorBytes - 4);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store64(invalid, 48, kDescriptorBytes + 4);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store64(invalid, 40, 126);
    controls->push_back(invalid);
    invalid = makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    store32(invalid, 24, 16385);
    store32(invalid, 28, 8192);
    store32(invalid, 32, 8192);
    controls->push_back(invalid);

    submitInvalidDescriptor(controls, 0);
  }

  void submitInvalidDescriptor(
      std::shared_ptr<std::vector<std::vector<uint8_t>>> controls,
      uint32_t index) {
    if (index == controls->size()) {
      verifyOversizedControlBytes();

      return;
    }

    driver.submitCSDReadCompute(
        controls->at(index), opcode, false, [this, controls, index](
                                               uint16_t status) {
          if (!hasStatus(status,
                         SimpleSSD::HIL::NVMe::TYPE_GENERIC_COMMAND_STATUS,
                         SimpleSSD::HIL::NVMe::STATUS_INVALID_FIELD)) {
            std::cerr << "invalid descriptor case " << index
                      << " returned status 0x" << std::hex << status
                      << std::dec << std::endl;
            fail("invalid read_compute descriptor returned wrong status");
          }
          else {
            submitInvalidDescriptor(controls, index + 1);
          }
        });
  }

  void verifyOversizedControlBytes() {
    uint64_t maxControl = config.readUint(
        SimpleSSD::CONFIG_CSD, SimpleSSD::CSD::CSD_MAX_CONTROL_BYTES);

    if (maxControl >= std::numeric_limits<uint32_t>::max()) {
      verifyInvalidOpcode();

      return;
    }

    driver.submitCSDReadComputeForNamespaceWithCommandBytes(
        smallControl, 1, (uint32_t)(maxControl + 1), opcode, false,
        [this](uint16_t status) {
          if (!hasStatus(status,
                         SimpleSSD::HIL::NVMe::TYPE_GENERIC_COMMAND_STATUS,
                         SimpleSSD::HIL::NVMe::STATUS_INVALID_FIELD)) {
            fail("oversized read_compute control buffer returned wrong status");
          }
          else {
            verifyInvalidOpcode();
          }
        });
  }

  void verifyInvalidOpcode() {
    std::vector<uint8_t> control =
        makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    uint8_t invalidOpcode = opcode ^ 0x01;

    driver.submitCSDReadCompute(
        control, invalidOpcode, false, [this](uint16_t status) {
          if (!hasStatus(status,
                         SimpleSSD::HIL::NVMe::TYPE_GENERIC_COMMAND_STATUS,
                         SimpleSSD::HIL::NVMe::STATUS_INVALID_OPCODE)) {
            fail("invalid read_compute opcode returned wrong status");
          }
          else {
            trimSmallMatrix();
          }
        });
  }

  void trimSmallMatrix() {
    driver.submitTrim(smallOffset, smallMatrix.size(),
                      [this](uint16_t status) {
      if (status != 0) {
        fail("ordinary NVMe trim failed");
      }
      else {
        verifyTrimmedAndInvalidControls();
      }
    });
  }

  void verifyTrimmedAndInvalidControls() {
    smallControl =
        makeControl(0, 3, 5, smallVector, 128, kDescriptorBytes, 80);
    driver.submitCSDReadCompute(
        smallControl, opcode, false, [this](uint16_t status) {
          if (!hasStatus(
                  status,
                  SimpleSSD::HIL::NVMe::TYPE_MEDIA_AND_DATA_INTEGRITY_ERROR,
                  SimpleSSD::HIL::NVMe::
                      STATUS_DEALLOCATED_OR_UNWRITTEN_LOGICAL_BLOCK)) {
            fail("trimmed read_compute returned wrong status");
          }
          else {
            verifyOutOfRange();
          }
        });
  }

  void verifyOutOfRange() {
    uint64_t lastLBA = capacity / lbaSize - 1;
    std::vector<uint8_t> rangeControl =
        makeControl(lastLBA, 128, 128, largeVector, 12288, 8192,
                    8192 + 128 * 2);

    driver.submitCSDReadCompute(
        rangeControl, opcode, false, [this](uint16_t status) {
          if (!hasStatus(status,
                         SimpleSSD::HIL::NVMe::TYPE_GENERIC_COMMAND_STATUS,
                         SimpleSSD::HIL::NVMe::STATUS_LBA_OUT_OF_RANGE)) {
            fail("out-of-range read_compute returned wrong status");
          }
          else {
            verifyInvalidNamespace();
          }
        });
  }

  void verifyInvalidNamespace() {
    std::vector<uint8_t> control =
        makeControl(largeOffset / lbaSize, 128, 128, largeVector, 12288,
                    8192, 8192 + 128 * 2);

    driver.submitCSDReadComputeForNamespace(
        control, 0, opcode, false, [this](uint16_t status) {
          if (!hasStatus(
                  status,
                  SimpleSSD::HIL::NVMe::TYPE_GENERIC_COMMAND_STATUS,
                  SimpleSSD::HIL::NVMe::STATUS_ABORT_INVALID_NAMESPACE)) {
            fail("invalid namespace read_compute returned wrong status");
          }
          else {
            verifyFormat();
          }
        });
  }

  void verifyFormat() {
    driver.submitFormat(false, [this](uint16_t status) {
      if (status != 0) {
        fail("ordinary NVMe format failed");
      }
      else {
        std::vector<uint8_t> control =
            makeControl(largeOffset / lbaSize, 128, 128, largeVector, 12288,
                        8192, 8192 + 128 * 2);

        driver.submitCSDReadCompute(
            control, opcode, false, [this](uint16_t computeStatus) {
              if (!hasStatus(
                      computeStatus,
                      SimpleSSD::HIL::NVMe::
                          TYPE_MEDIA_AND_DATA_INTEGRITY_ERROR,
                      SimpleSSD::HIL::NVMe::
                          STATUS_DEALLOCATED_OR_UNWRITTEN_LOGICAL_BLOCK)) {
                fail("format read_compute returned wrong status");
              }
              else {
                succeed();
              }
            });
      }
    });
  }

 public:
  CSDIntegration(Engine &e, SimpleSSD::ConfigReader &c,
                 uint8_t readComputeOpcode)
      : engine(e),
        config(c),
        driver(engine, config),
        opcode(readComputeOpcode),
        capacity(0),
        lbaSize(0),
        finished(false),
        result(1),
        smallOffset(0),
        largeOffset(0) {}

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
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: simplessd-csd-integration <SimpleSSD config file> "
                 "[read_compute opcode]"
              << std::endl;
    return 1;
  }

  uint8_t opcode = 0xC0;

  if (argc == 3) {
    char *end = nullptr;
    unsigned long parsed = strtoul(argv[2], &end, 0);

    if (!end || *end != '\0' || parsed > 0xFF) {
      std::cerr << "invalid read_compute opcode" << std::endl;
      return 1;
    }

    opcode = (uint8_t)parsed;
  }

  Engine engine;
  auto config = initSimpleSSDEngine(&engine, nullptr, nullptr, argv[1]);
  CSDIntegration test(engine, config, opcode);
  int result = test.run();

  releaseSimpleSSDEngine();

  return result;
}
