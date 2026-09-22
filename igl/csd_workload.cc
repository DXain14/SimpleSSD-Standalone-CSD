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

#include "igl/csd_workload.hh"

#include <cmath>
#include <limits>

#include "simplessd/sim/trace.hh"

namespace IGL {

namespace CSDWorkload {

namespace {

const uint64_t DESCRIPTOR_BYTES = 56;
const uint64_t VECTOR_OFFSET = DESCRIPTOR_BYTES;

}  // namespace

bool checkedAdd(uint64_t a, uint64_t b, uint64_t &out) {
  if (a > std::numeric_limits<uint64_t>::max() - b) {
    return false;
  }

  out = a + b;

  return true;
}

bool checkedMul(uint64_t a, uint64_t b, uint64_t &out) {
  if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) {
    return false;
  }

  out = a * b;

  return true;
}

uint64_t alignUp(uint64_t value, uint64_t align) {
  uint64_t add = 0;

  if (align == 0 || !checkedAdd(value, align - 1, add)) {
    SimpleSSD::panic("csd_workload: alignment overflow");
  }

  return add / align * align;
}

Shape validateShape(uint64_t rows, uint64_t cols, const char *where) {
  Shape shape;
  uint64_t elements = 0;
  uint64_t matrixBytes = 0;
  uint64_t outputBytes = 0;

  if (rows == 0 || cols == 0 || rows > std::numeric_limits<uint32_t>::max() ||
      cols > std::numeric_limits<uint32_t>::max()) {
    SimpleSSD::panic("%s: invalid CSD GEMV rows/cols", where);
  }

  if (!checkedMul(rows, cols, elements) ||
      !checkedMul(elements, sizeof(uint16_t), matrixBytes) ||
      !checkedMul(rows, sizeof(float), outputBytes)) {
    SimpleSSD::panic("%s: CSD GEMV dimension overflow", where);
  }

  shape.rows = (uint32_t)rows;
  shape.cols = (uint32_t)cols;
  shape.elements = elements;
  shape.matrixBytes = matrixBytes;
  shape.outputBytes = outputBytes;

  return shape;
}

uint64_t transferBytes(uint64_t bytes, uint64_t lbaSize, const char *where) {
  uint64_t add = 0;

  if (bytes == 0 || lbaSize == 0 ||
      !checkedAdd(bytes, lbaSize - 1, add)) {
    SimpleSSD::panic("%s: CSD transfer size overflow", where);
  }

  return add / lbaSize * lbaSize;
}

uint64_t transferLBAs(uint64_t bytes, uint64_t lbaSize, const char *where) {
  uint64_t ret = transferBytes(bytes, lbaSize, where) / lbaSize;

  if (ret == 0) {
    SimpleSSD::panic("%s: invalid CSD transfer LBA count", where);
  }

  return ret;
}

void validatePlacement(uint64_t matrixSLBA, uint64_t transferLBAs,
                       uint64_t matrixCount, uint64_t totalLBAs,
                       const char *where) {
  uint64_t totalMatrixLBAs;

  if (matrixCount == 0 || transferLBAs == 0 ||
      !checkedMul(transferLBAs, matrixCount, totalMatrixLBAs) ||
      matrixSLBA > totalLBAs || totalMatrixLBAs > totalLBAs - matrixSLBA) {
    SimpleSSD::panic("%s: invalid CSD matrix placement", where);
  }
}

void validateControlBytes(uint32_t rows, uint32_t cols, uint64_t controlBytes,
                          const char *where) {
  uint64_t vectorBytes;
  uint64_t outputBytes;
  uint64_t outputOffset;
  uint64_t minimumControlBytes;

  if (!checkedMul(cols, sizeof(uint16_t), vectorBytes) ||
      !checkedMul(rows, sizeof(float), outputBytes)) {
    SimpleSSD::panic("%s: CSD control dimension overflow", where);
  }

  outputOffset = alignUp(VECTOR_OFFSET + vectorBytes, sizeof(float));

  if (!checkedAdd(outputOffset, outputBytes, minimumControlBytes) ||
      minimumControlBytes > std::numeric_limits<uint32_t>::max() ||
      (controlBytes != 0 && controlBytes < minimumControlBytes) ||
      controlBytes > std::numeric_limits<uint32_t>::max()) {
    SimpleSSD::panic("%s: invalid CSD control buffer size", where);
  }
}

uint16_t makeHalf(uint64_t index) {
  static const uint16_t values[] = {
      0x3800, 0x3C00, 0x4000, 0x4200, 0xBC00, 0xC000,
  };

  return values[index % (sizeof(values) / sizeof(values[0]))];
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

std::shared_ptr<std::vector<uint8_t>> makeMatrixPayload(uint32_t rows,
                                                        uint32_t cols,
                                                        uint64_t bytes,
                                                        uint64_t seed) {
  auto payload = std::make_shared<std::vector<uint8_t>>(bytes, 0);

  for (uint64_t i = 0; i < (uint64_t)rows * cols; i++) {
    uint16_t value = makeHalf(seed + i);
    uint64_t offset = i * sizeof(uint16_t);

    payload->at(offset + 0) = value & 0xFF;
    payload->at(offset + 1) = (value >> 8) & 0xFF;
  }

  return payload;
}

std::vector<uint16_t> makeVector(uint32_t cols, uint64_t seed) {
  std::vector<uint16_t> vector(cols);

  for (uint32_t i = 0; i < cols; i++) {
    vector[i] = makeHalf(seed + (uint64_t)i * 3);
  }

  return vector;
}

std::vector<float> referenceGEMV(const std::vector<uint8_t> &matrix,
                                 uint32_t rows, uint32_t cols,
                                 const std::vector<uint16_t> &vector) {
  std::vector<float> ret(rows, 0.f);

  if (vector.size() != cols ||
      matrix.size() < (uint64_t)rows * cols * sizeof(uint16_t)) {
    SimpleSSD::panic("csd_workload: invalid reference GEMV inputs");
  }

  for (uint32_t row = 0; row < rows; row++) {
    for (uint32_t col = 0; col < cols; col++) {
      uint64_t offset = ((uint64_t)row * cols + col) * sizeof(uint16_t);
      uint16_t raw = (uint16_t)matrix[offset] |
                     ((uint16_t)matrix[offset + 1] << 8);

      ret[row] += decodeFP16(raw) * decodeFP16(vector[col]);
    }
  }

  return ret;
}

bool outputMatches(const std::vector<float> &actual,
                   const std::vector<float> &expected,
                   std::string *message) {
  if (actual.size() != expected.size()) {
    if (message) {
      *message = "output length mismatch";
    }

    return false;
  }

  for (uint32_t i = 0; i < expected.size(); i++) {
    float absTol = std::max(0.001f, std::fabs(expected[i]) * 0.0001f);

    if (std::isnan(expected[i])) {
      if (!std::isnan(actual[i])) {
        if (message) {
          *message = "expected NaN output";
        }

        return false;
      }
    }
    else if (std::isinf(expected[i])) {
      if (!std::isinf(actual[i]) ||
          std::signbit(actual[i]) != std::signbit(expected[i])) {
        if (message) {
          *message = "expected infinite output";
        }

        return false;
      }
    }
    else if (std::fabs(actual[i] - expected[i]) > absTol) {
      if (message) {
        *message = "GEMV output mismatch";
      }

      return false;
    }
  }

  return true;
}

}  // namespace CSDWorkload

}  // namespace IGL
