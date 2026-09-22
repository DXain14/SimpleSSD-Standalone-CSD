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
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "sil/nvme/nvme.hh"
#include "sim/engine.hh"
#include "simplessd/util/simplessd.hh"

namespace {

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

uint32_t load32(const std::vector<uint8_t> &buffer, uint64_t offset) {
  return (uint32_t)buffer[offset + 0] | ((uint32_t)buffer[offset + 1] << 8) |
         ((uint32_t)buffer[offset + 2] << 16) |
         ((uint32_t)buffer[offset + 3] << 24);
}

float loadFloat(const std::vector<uint8_t> &buffer, uint64_t offset) {
  uint32_t raw = load32(buffer, offset);
  float value;

  memcpy(&value, &raw, sizeof(value));

  return value;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: simplessd-csd-smoke <SimpleSSD config file>"
              << std::endl;

    return 1;
  }

  Engine engine;
  auto ssdConfig = initSimpleSSDEngine(&engine, nullptr, &std::cerr, argv[1]);
  SIL::NVMe::Driver driver(engine, ssdConfig);

  std::vector<uint8_t> matrix(512, 0);
  const uint16_t matrixFP16[] = {
      0x3C00,  // 1
      0x4000,  // 2
      0x4200,  // 3
      0x4400,  // 4
      0x4500,  // 5
      0x4600,  // 6
  };

  for (uint32_t i = 0; i < 6; i++) {
    store16(matrix, i * 2, matrixFP16[i]);
  }

  const uint64_t vectorOffset = 56;
  const uint64_t outputOffset = vectorOffset + 3 * 2;
  std::vector<uint8_t> control(outputOffset + 2 * 4, 0);

  control[0] = 'C';
  control[1] = 'S';
  control[2] = 'D';
  control[3] = '0';
  store16(control, 4, 1);          // version
  store16(control, 6, 1);          // GEMV
  store32(control, 8, 0);          // flags
  store64(control, 16, 0);         // matrix SLBA
  store32(control, 24, 2);         // rows
  store32(control, 28, 3);         // cols
  store32(control, 32, 3);         // lda
  store64(control, 40, vectorOffset);
  store64(control, 48, outputOffset);
  store16(control, vectorOffset + 0, 0x3800);  // 0.5
  store16(control, vectorOffset + 2, 0x3C00);  // 1.0
  store16(control, vectorOffset + 4, 0xBC00);  // -1.0

  bool finished = false;
  int result = 1;

  std::function<void()> begin = [&]() {
    driver.submitWriteBuffer(0, matrix.data(), matrix.size(),
                             [&](uint16_t status) {
      if (status != 0) {
        std::cerr << "matrix write failed: 0x" << std::hex << status
                  << std::dec << std::endl;
        finished = true;
        result = 2;
        engine.stopEngine();

        return;
      }

      driver.submitCSDReadCompute(control, [&](uint16_t status) {
        if (status != 0) {
          std::cerr << "read_compute failed: 0x" << std::hex << status
                    << std::dec << std::endl;
          finished = true;
          result = 3;
          engine.stopEngine();

          return;
        }

        float y0 = loadFloat(control, outputOffset + 0);
        float y1 = loadFloat(control, outputOffset + 4);

        if (std::fabs(y0 - (-0.5f)) > 0.001f ||
            std::fabs(y1 - 1.0f) > 0.001f) {
          std::cerr << "unexpected GEMV output: " << y0 << ", " << y1
                    << std::endl;
          result = 4;
        }
        else {
          std::cout << "CSD smoke GEMV passed: " << y0 << ", " << y1
                    << std::endl;
          result = 0;
        }

        finished = true;
        engine.stopEngine();
      });
    });
  };

  driver.init(begin);

  while (engine.doNextEvent())
    ;

  releaseSimpleSSDEngine();

  if (!finished) {
    std::cerr << "CSD smoke did not finish" << std::endl;

    return 5;
  }

  return result;
}
