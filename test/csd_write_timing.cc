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

#include <cinttypes>
#include <functional>
#include <iostream>
#include <vector>

#include "sil/nvme/nvme.hh"
#include "sim/engine.hh"
#include "simplessd/util/simplessd.hh"

namespace {

class WriteTimingTest {
 private:
  Engine &engine;
  SimpleSSD::ConfigReader &config;
  SIL::NVMe::Driver driver;
  uint64_t startTick;
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
    if (lbaSize == 0 || capacity < (uint64_t)lbaSize * 64) {
      fail("write timing test namespace is too small");

      return;
    }

    std::vector<uint8_t> payload(lbaSize * 64, 0);

    for (uint64_t i = 0; i < payload.size(); i++) {
      payload[i] = (uint8_t)(i * 17 + 3);
    }

    startTick = SimpleSSD::getTick();
    driver.submitWriteBuffer(0, payload.data(), payload.size(),
                             [this](uint16_t status) {
      if (status != 0) {
        fail("ordinary write failed");
      }
      else {
        uint64_t delta = SimpleSSD::getTick() - startTick;

        std::cout << "WriteCompletionDelta: " << delta << std::endl;
        finished = true;
        result = 0;
        engine.stopEngine();
      }
    });
  }

 public:
  WriteTimingTest(Engine &e, SimpleSSD::ConfigReader &c)
      : engine(e),
        config(c),
        driver(engine, config),
        startTick(0),
        finished(false),
        result(1) {}

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
    std::cerr << "Usage: simplessd-csd-write-timing <SimpleSSD config file>"
              << std::endl;
    return 1;
  }

  Engine engine;
  auto config = initSimpleSSDEngine(&engine, nullptr, nullptr, argv[1]);
  WriteTimingTest test(engine, config);
  int result = test.run();

  releaseSimpleSSDEngine();

  return result;
}
