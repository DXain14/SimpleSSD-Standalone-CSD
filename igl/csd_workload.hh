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

#pragma once

#ifndef __IGL_CSD_WORKLOAD__
#define __IGL_CSD_WORKLOAD__

#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

namespace IGL {

namespace CSDWorkload {

struct Shape {
  uint32_t rows;
  uint32_t cols;
  uint64_t elements;
  uint64_t matrixBytes;
  uint64_t outputBytes;
};

bool checkedAdd(uint64_t, uint64_t, uint64_t &);
bool checkedMul(uint64_t, uint64_t, uint64_t &);
uint64_t alignUp(uint64_t, uint64_t);
Shape validateShape(uint64_t, uint64_t, const char *);
uint64_t transferBytes(uint64_t, uint64_t, const char *);
uint64_t transferLBAs(uint64_t, uint64_t, const char *);
void validatePlacement(uint64_t, uint64_t, uint64_t, uint64_t, const char *);
void validateControlBytes(uint32_t, uint32_t, uint64_t, const char *);

uint16_t makeHalf(uint64_t);
float decodeFP16(uint16_t);
std::shared_ptr<std::vector<uint8_t>> makeMatrixPayload(uint32_t, uint32_t,
                                                        uint64_t, uint64_t);
std::vector<uint16_t> makeVector(uint32_t, uint64_t);
std::vector<float> referenceGEMV(const std::vector<uint8_t> &, uint32_t,
                                 uint32_t,
                                 const std::vector<uint16_t> &);
bool outputMatches(const std::vector<float> &, const std::vector<float> &,
                   std::string *);

}  // namespace CSDWorkload

}  // namespace IGL

#endif
