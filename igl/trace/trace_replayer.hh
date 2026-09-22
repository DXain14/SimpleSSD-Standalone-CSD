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

#ifndef __IGL_TRACE_REPLAYER__
#define __IGL_TRACE_REPLAYER__

#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <regex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "bil/entry.hh"
#include "igl/io_gen.hh"
#include "sim/cfg_reader.hh"
#include "sim/engine.hh"

namespace IGL {

class TraceReplayer : public IOGenerator {
 private:
  enum {
    ID_OPERATION,
    ID_BYTE_OFFSET,
    ID_BYTE_LENGTH,
    ID_LBA_OFFSET,
    ID_LBA_LENGTH,
    ID_TIME_SEC,
    ID_TIME_MS,
    ID_TIME_US,
    ID_TIME_NS,
    ID_TIME_PS,
    ID_MATRIX_SLBA,
    ID_ROWS,
    ID_COLS,
    ID_VECTOR_SEED,
    ID_MATRIX_SEED,
    ID_NUM
  };

  std::mutex m;

  std::ifstream file;
  std::regex regex;

  uint64_t fileSize;

  TIMING_MODE mode;
  uint64_t submissionLatency;
  uint64_t completionLatency;
  uint32_t maxQueueDepth;  // Only used in MODE_ASYNC

  bool useLBAOffset;
  bool useLBALength;
  uint32_t lbaSize;
  uint32_t groupID[ID_NUM];
  bool timeValids[5];
  bool useHex;
  uint8_t csdOpcode;
  bool csdUseSGL;
  bool csdVerifyOutput;

  uint64_t ssdSize;
  uint32_t blocksize;

  uint64_t initTime;   // Only used in MODE_STRICT
  uint64_t firstTick;  // Only used in MODE_STRICT
  bool nextIOIsSync;   // Only used in MODE_ASYNC

  bool reserveTermination;

  uint64_t max_io;
  uint64_t io_submitted;  // Submitted I/O in bytes
  uint64_t io_count;      // I/O count created and submitted
  uint64_t read_count;
  uint64_t write_count;
  uint64_t compute_count;
  uint64_t verified_compute_count;
  uint64_t failed_compute_count;

  uint64_t io_depth;

  uint64_t mergeTime(std::smatch &);
  uint64_t parseInteger(std::smatch &, uint32_t, const char *);
  BIL::BIO_TYPE getType(std::string);
  void parseLine();
  void rescheduleSubmit(uint64_t);

  struct MatrixInfo {
    uint64_t matrixSLBA;
    uint32_t rows;
    uint32_t cols;
    uint64_t matrixBytes;
    uint64_t transferBytes;
    uint64_t transferLBAs;
    uint64_t seed;
    std::shared_ptr<std::vector<uint8_t>> payload;

    MatrixInfo()
        : matrixSLBA(0),
          rows(0),
          cols(0),
          matrixBytes(0),
          transferBytes(0),
          transferLBAs(0),
          seed(0) {}
  };

  std::unordered_map<uint64_t, MatrixInfo> matrices;
  std::unordered_map<uint64_t, MatrixInfo> pendingMatrixPreloads;
  std::unordered_map<uint64_t, std::shared_ptr<BIL::CSDGEMVRequest>>
      pendingCSD;

  struct TraceLine {
    uint64_t tick;
    uint64_t offset;
    uint64_t length;
    BIL::BIO_TYPE type;
    bool csdMatrixPreload;
    uint64_t matrixSLBA;
    uint32_t rows;
    uint32_t cols;
    uint64_t vectorSeed;
    uint64_t matrixSeed;
    uint64_t matrixBytes;
    uint64_t matrixTransferBytes;
    uint64_t matrixTransferLBAs;

    TraceLine()
        : tick(0),
          offset(0),
          length(0),
          type(BIL::BIO_NUM),
          csdMatrixPreload(false),
          matrixSLBA(0),
          rows(0),
          cols(0),
          vectorSeed(0),
          matrixSeed(0),
          matrixBytes(0),
          matrixTransferBytes(0),
          matrixTransferLBAs(0) {}
  } linedata;

  SimpleSSD::Event submitEvent;
  std::function<void(uint64_t, uint16_t)> completionEvent;

  void submitIO();
  void iocallback(uint64_t, uint16_t);

 public:
  TraceReplayer(Engine &, BIL::BlockIOEntry &, std::function<void()> &,
                ConfigReader &);
  ~TraceReplayer();

  void init(uint64_t, uint32_t) override;
  void begin() override;
  void printStats(std::ostream &) override;
  void getProgress(float &) override;
};

}  // namespace IGL

#endif
