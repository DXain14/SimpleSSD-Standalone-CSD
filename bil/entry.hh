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

#ifndef __BIL_ENTRY__
#define __BIL_ENTRY__

#include <cinttypes>
#include <fstream>
#include <functional>
#include <list>
#include <memory>
#include <vector>

#include "sim/cfg_reader.hh"
#include "sim/engine.hh"

namespace BIL {

class Scheduler;
class DriverInterface;

enum BIO_TYPE : uint8_t {
  BIO_READ,
  BIO_WRITE,
  BIO_FLUSH,
  BIO_TRIM,
  BIO_READ_COMPUTE,
  BIO_NUM,
};

typedef struct _CSDGEMVRequest {
  uint64_t matrixSLBA;
  uint32_t rows;
  uint32_t cols;
  uint32_t lda;
  std::vector<uint16_t> vectorFP16;
  std::vector<float> outputFP32;
  std::vector<float> expectedFP32;
  uint8_t opcode;
  bool useSGL;
  bool verifyOutput;
  uint64_t controlBytes;

  _CSDGEMVRequest()
      : matrixSLBA(0),
        rows(0),
        cols(0),
        lda(0),
        opcode(0xC0),
        useSGL(false),
        verifyOutput(false),
        controlBytes(0) {}
} CSDGEMVRequest;

typedef struct _BIO {
  uint64_t id;

  // I/O definition
  BIO_TYPE type;
  uint64_t offset;
  uint64_t length;
  std::shared_ptr<std::vector<uint8_t>> payload;
  std::shared_ptr<CSDGEMVRequest> csd;

  // I/O completion
  std::function<void(uint64_t, uint16_t)> callback;

  // Statistics
  uint64_t submittedAt;

  _BIO() : id(0), type(BIO_READ), offset(0), length(0), submittedAt(0) {}
} BIO;

typedef struct _Progress {
  uint64_t iops;
  uint64_t bandwidth;
  uint64_t latency;
} Progress;

class BlockIOEntry {
 private:
  ConfigReader &conf;
  Engine &engine;
  std::list<BIO> ioQueue;

  std::ostream *pLatencyFile;

  Scheduler *pScheduler;
  DriverInterface *pDriver;

  std::mutex m;
  uint64_t lastProgress;
  uint64_t io_progress;
  Progress progress;

  // Statistics
  uint64_t io_count;
  uint64_t error_count;
  uint64_t minLatency;
  uint64_t maxLatency;
  uint64_t sumLatency;
  uint64_t squareSumLatency;

  std::function<void(uint64_t, uint16_t)> callback;
  void completion(uint64_t, uint16_t);

 public:
  BlockIOEntry(ConfigReader &, Engine &, DriverInterface *, std::ostream *);
  ~BlockIOEntry();

  void submitIO(BIO &);

  void printStats(std::ostream &);
  void getProgress(Progress &);
};

}  // namespace BIL

#endif
