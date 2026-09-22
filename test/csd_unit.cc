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
#include <cstdint>
#include <iostream>
#include <vector>

#include "simplessd/csd/flash_array.hh"
#include "simplessd/csd/pu.hh"

namespace {

bool near(float a, float b) {
  return std::fabs(a - b) < 0.0001f;
}

bool checkFP16() {
  using SimpleSSD::CSD::PU;

  return near(PU::decodeFP16(0x0000), 0.f) &&
         near(PU::decodeFP16(0x3C00), 1.f) &&
         near(PU::decodeFP16(0xC000), -2.f) &&
         near(PU::decodeFP16(0x0001), std::ldexp(1.f, -24)) &&
         std::isinf(PU::decodeFP16(0x7C00)) &&
         std::isnan(PU::decodeFP16(0x7E00));
}

bool checkFlashArrayStore() {
  SimpleSSD::CSD::FlashArrayStore store;
  std::vector<uint8_t> input = {1, 2, 3, 4};
  std::vector<uint8_t> full(16, 0xAA);
  std::vector<uint8_t> partial(4, 0);
  std::vector<uint8_t> aliasA = {0x10, 0x11, 0x12, 0x13};
  std::vector<uint8_t> aliasB = {0x20, 0x21, 0x22, 0x23};
  std::vector<uint8_t> aliasOut(4, 0);

  store.setUnitSize(16);

  if (!store.write(1, 2, 3, 2, input.data(), input.size())) {
    return false;
  }
  if (!store.read(1, 2, 3, 0, full.data(), full.size())) {
    return false;
  }
  if (full[0] != 0 || full[1] != 0 || full[2] != 1 || full[3] != 2 ||
      full[4] != 3 || full[5] != 4 || full[6] != 0 || full[15] != 0) {
    return false;
  }

  if (!store.copy(1, 2, 3, 4, 5, 6)) {
    return false;
  }
  if (!store.read(4, 5, 6, 2, partial.data(), partial.size())) {
    return false;
  }
  for (uint32_t i = 0; i < partial.size(); i++) {
    if (partial[i] != input[i]) {
      return false;
    }
  }

  store.erase(1, 2, 3);
  if (store.read(1, 2, 3, 0, full.data(), full.size())) {
    return false;
  }
  if (!store.read(4, 5, 6, 2, partial.data(), partial.size())) {
    return false;
  }

  store.eraseBlock(4);

  if (store.read(4, 5, 6, 0, full.data(), full.size())) {
    return false;
  }

  store.clear();
  store.setUnitSize(4);

  if (!store.write(1, 0, 0, 0, aliasA.data(), aliasA.size()) ||
      !store.write(0, 65536, 0, 0, aliasB.data(), aliasB.size())) {
    return false;
  }
  if (!store.read(1, 0, 0, 0, aliasOut.data(), aliasOut.size()) ||
      aliasOut != aliasA) {
    return false;
  }
  if (!store.read(0, 65536, 0, 0, aliasOut.data(), aliasOut.size()) ||
      aliasOut != aliasB) {
    return false;
  }

  store.eraseBlock(1);
  if (store.read(1, 0, 0, 0, aliasOut.data(), aliasOut.size())) {
    return false;
  }
  if (!store.read(0, 65536, 0, 0, aliasOut.data(), aliasOut.size()) ||
      aliasOut != aliasB) {
    return false;
  }

  store.clear();
  if (!store.write(0, 1, 0, 0, aliasA.data(), aliasA.size()) ||
      !store.write(0, 0, 65536, 0, aliasB.data(), aliasB.size())) {
    return false;
  }
  if (!store.read(0, 1, 0, 0, aliasOut.data(), aliasOut.size()) ||
      aliasOut != aliasA) {
    return false;
  }

  return store.read(0, 0, 65536, 0, aliasOut.data(), aliasOut.size()) &&
         aliasOut == aliasB;
}

}  // namespace

int main() {
  if (!checkFP16()) {
    std::cerr << "FP16 decode unit test failed" << std::endl;

    return 1;
  }

  if (!checkFlashArrayStore()) {
    std::cerr << "FlashArrayStore unit test failed" << std::endl;

    return 2;
  }

  std::cout << "CSD unit tests passed" << std::endl;

  return 0;
}
