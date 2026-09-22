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

#include "igl/trace/trace_replayer.hh"

#include <cerrno>
#include <cstdlib>
#include <memory>

#include "igl/csd_workload.hh"
#include "simplessd/sim/trace.hh"
#include "simplessd/util/algorithm.hh"

namespace IGL {

TraceReplayer::TraceReplayer(Engine &e, BIL::BlockIOEntry &b,
                             std::function<void()> &f, ConfigReader &c)
    : IOGenerator(e, b, f),
      useLBAOffset(false),
      useLBALength(false),
      nextIOIsSync(false),
      reserveTermination(false),
      io_submitted(0),
      io_count(0),
      read_count(0),
      write_count(0),
      compute_count(0),
      verified_compute_count(0),
      failed_compute_count(0),
      io_depth(0) {
  // Check file
  auto filename = c.readString(CONFIG_TRACE, TRACE_FILE);
  file.open(filename);

  if (!file.is_open()) {
    SimpleSSD::panic("Failed to open trace file %s!", filename.c_str());
  }

  file.seekg(0, std::ios::end);
  fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // Create regex
  try {
    regex = std::regex(c.readString(CONFIG_TRACE, TRACE_LINE_REGEX));
  }
  catch (std::regex_error &e) {
    SimpleSSD::panic("Invalid regular expression!");
  }

  // Fill flags
  mode = (TIMING_MODE)c.readUint(CONFIG_TRACE, TRACE_TIMING_MODE);
  submissionLatency = c.readUint(CONFIG_GLOBAL, GLOBAL_SUBMISSION_LATENCY);
  completionLatency = c.readUint(CONFIG_GLOBAL, GLOBAL_COMPLETION_LATENCY);
  maxQueueDepth = c.readUint(CONFIG_TRACE, TRACE_QUEUE_DEPTH);
  max_io = c.readUint(CONFIG_TRACE, TRACE_IO_LIMIT);
  groupID[ID_OPERATION] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_OPERATION);
  groupID[ID_BYTE_OFFSET] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_BYTE_OFFSET);
  groupID[ID_BYTE_LENGTH] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_BYTE_LENGTH);
  groupID[ID_LBA_OFFSET] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_LBA_OFFSET);
  groupID[ID_LBA_LENGTH] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_LBA_LENGTH);
  groupID[ID_TIME_SEC] = (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_SEC);
  groupID[ID_TIME_MS] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_MILI_SEC);
  groupID[ID_TIME_US] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_MICRO_SEC);
  groupID[ID_TIME_NS] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_NANO_SEC);
  groupID[ID_TIME_PS] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_PICO_SEC);
  groupID[ID_MATRIX_SLBA] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_MATRIX_SLBA);
  groupID[ID_ROWS] = (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_ROWS);
  groupID[ID_COLS] = (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_COLS);
  groupID[ID_VECTOR_SEED] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_VECTOR_SEED);
  groupID[ID_MATRIX_SEED] =
      (uint32_t)c.readUint(CONFIG_TRACE, TRACE_GROUP_MATRIX_SEED);
  useHex = c.readBoolean(CONFIG_TRACE, TRACE_USE_HEX);
  csdOpcode = (uint8_t)c.readUint(CONFIG_TRACE, TRACE_CSD_OPCODE);
  csdUseSGL = c.readBoolean(CONFIG_TRACE, TRACE_CSD_USE_SGL);
  csdVerifyOutput = c.readBoolean(CONFIG_TRACE, TRACE_CSD_VERIFY_OUTPUT);

  if (groupID[ID_OPERATION] == 0) {
    SimpleSSD::panic("Operation group ID cannot be 0");
  }

  if (groupID[ID_LBA_OFFSET] > 0) {
    useLBAOffset = true;
  }
  if (groupID[ID_LBA_LENGTH] > 0) {
    useLBALength = true;
  }

  bool hasCSDBaseGroups = groupID[ID_MATRIX_SLBA] > 0 &&
                          groupID[ID_ROWS] > 0 && groupID[ID_COLS] > 0;
  bool hasCSDComputeGroups = hasCSDBaseGroups && groupID[ID_VECTOR_SEED] > 0;
  bool hasCSDMatrixGroups = hasCSDBaseGroups && groupID[ID_MATRIX_SEED] > 0;
  bool hasAnyCSDGroup = groupID[ID_MATRIX_SLBA] > 0 ||
                        groupID[ID_ROWS] > 0 || groupID[ID_COLS] > 0 ||
                        groupID[ID_VECTOR_SEED] > 0 ||
                        groupID[ID_MATRIX_SEED] > 0;
  bool hasCSDGroups = hasCSDComputeGroups || hasCSDMatrixGroups;

  if ((hasAnyCSDGroup || csdVerifyOutput) &&
      c.readUint(CONFIG_GLOBAL, GLOBAL_INTERFACE) != INTERFACE_NVME) {
    SimpleSSD::panic("CSD trace read_compute requires Interface = 1 (NVMe)");
  }

  if (useLBALength || useLBAOffset || hasCSDGroups) {
    lbaSize = (uint32_t)c.readUint(CONFIG_TRACE, TRACE_LBA_SIZE);

    if (SimpleSSD::popcount(lbaSize) != 1) {
      SimpleSSD::panic("LBA size should be power of 2");
    }
  }

  bool hasAddressGroups =
      (useLBAOffset || groupID[ID_BYTE_OFFSET] > 0) &&
      (useLBALength || groupID[ID_BYTE_LENGTH] > 0);

  if (!hasAddressGroups && !hasCSDGroups) {
    SimpleSSD::panic(
        "Trace requires block address groups or CSD matrix groups");
  }
  if (hasAnyCSDGroup && !hasCSDGroups) {
    SimpleSSD::panic("Incomplete CSD trace group configuration");
  }
  if (csdVerifyOutput && !hasCSDMatrixGroups) {
    SimpleSSD::panic("CSD trace output verification requires MatrixSeed group");
  }

  timeValids[0] = groupID[ID_TIME_SEC] > 0 ? true : false;
  timeValids[1] = groupID[ID_TIME_MS] > 0 ? true : false;
  timeValids[2] = groupID[ID_TIME_US] > 0 ? true : false;
  timeValids[3] = groupID[ID_TIME_NS] > 0 ? true : false;
  timeValids[4] = groupID[ID_TIME_PS] > 0 ? true : false;

  if (!(timeValids[0] || timeValids[1] || timeValids[2] || timeValids[3] ||
        timeValids[4])) {
    if (mode == MODE_STRICT) {
      SimpleSSD::panic("No valid time field specified");
    }
  }

  firstTick = std::numeric_limits<uint64_t>::max();

  completionEvent = [this](uint64_t id, uint16_t status) {
    iocallback(id, status);
  };

  submitEvent = engine.allocateEvent([this](uint64_t) { submitIO(); });
}

TraceReplayer::~TraceReplayer() {
  file.close();
}

void TraceReplayer::init(uint64_t bytesize, uint32_t bs) {
  ssdSize = bytesize;
  blocksize = bs;

  if ((useLBALength || useLBAOffset) && lbaSize < bs) {
    SimpleSSD::warn("LBA size of trace file is smaller than SSD's LBA size");
  }
}

void TraceReplayer::begin() {
  initTime = engine.getCurrentTick();

  parseLine();

  if (mode == MODE_STRICT) {
    firstTick = linedata.tick;
  }
  else {
    firstTick = initTime;
  }

  if (reserveTermination) {
    SimpleSSD::warn("No I/O submitted. Check regular expression.");

    endCallback();
  }
  else {
    submitIO();
  }
}

void TraceReplayer::printStats(std::ostream &out) {
  uint64_t tick = engine.getCurrentTick();

  out << "*** Statistics of Trace Replayer ***" << std::endl;
  out << "Tick: " << tick << std::endl;
  out << "Time (ps): " << firstTick - initTime << " - " << tick << " ("
      << tick + firstTick - initTime << ")" << std::endl;
  out << "I/O (bytes): " << io_submitted << " ("
      << std::to_string((double)io_submitted / tick * 1000000000000.) << " B/s)"
      << std::endl;
  out << "I/O (counts): " << io_count << " (Read: " << read_count
      << ", Write: " << write_count << ", ReadCompute: " << compute_count
      << ")" << std::endl;
  out << "VerifiedReadCompute: " << verified_compute_count << std::endl;
  out << "FailedReadCompute: " << failed_compute_count << std::endl;
  out << "*** End of statistics ***" << std::endl;

  bioEntry.printStats(out);
}

void TraceReplayer::getProgress(float &val) {
  if (max_io == 0) {
    // If I/O count is unlimited, use file pointer for fast progress calculation
    uint64_t ptr;

    {
      std::lock_guard<std::mutex> guard(m);
      ptr = file.tellg();
    }

    val = (float)ptr / fileSize;
  }
  else {
    // Use submitted I/O count in progress calculation
    // If trace file contains I/O requests smaller than max_io, progress value
    // cannot reach 1.0 (100%)
    val = (float)io_count / max_io;
  }
}

uint64_t TraceReplayer::mergeTime(std::smatch &match) {
  uint64_t tick = 0;
  bool valid = true;

  if (timeValids[0] && match.size() > groupID[ID_TIME_SEC]) {
    tick += strtoul(match[groupID[ID_TIME_SEC]].str().c_str(), nullptr, 10) *
            1000000000000ULL;
  }
  else if (timeValids[0]) {
    valid = false;
  }

  if (timeValids[1] && match.size() > groupID[ID_TIME_MS]) {
    tick += strtoul(match[groupID[ID_TIME_MS]].str().c_str(), nullptr, 10) *
            1000000000ULL;
  }
  else if (timeValids[1]) {
    valid = false;
  }

  if (timeValids[2] && match.size() > groupID[ID_TIME_US]) {
    tick += strtoul(match[groupID[ID_TIME_US]].str().c_str(), nullptr, 10) *
            1000000ULL;
  }
  else if (timeValids[2]) {
    valid = false;
  }

  if (timeValids[3] && match.size() > groupID[ID_TIME_NS]) {
    tick += strtoul(match[groupID[ID_TIME_NS]].str().c_str(), nullptr, 10) *
            1000ULL;
  }
  else if (timeValids[3]) {
    valid = false;
  }

  if (timeValids[4] && match.size() > groupID[ID_TIME_PS]) {
    tick += strtoul(match[groupID[ID_TIME_PS]].str().c_str(), nullptr, 10);
  }
  else if (timeValids[4]) {
    valid = false;
  }

  if (!valid) {
    SimpleSSD::panic("Time parse failed");
  }

  return tick;
}

uint64_t TraceReplayer::parseInteger(std::smatch &match, uint32_t group,
                                     const char *name) {
  if (group == 0 || match.size() <= group) {
    SimpleSSD::panic("Trace field %s parse failed", name);
  }

  std::string text = match[group].str();
  char *end = nullptr;

  errno = 0;
  uint64_t ret = strtoull(text.c_str(), &end, useHex ? 16 : 10);

  if (errno == ERANGE || end == text.c_str() || *end != '\0') {
    SimpleSSD::panic("Trace field %s parse failed", name);
  }

  return ret;
}

BIL::BIO_TYPE TraceReplayer::getType(std::string type) {
  io_count++;

  switch (type[0]) {
    case 'r':
    case 'R':
      read_count++;

      return BIL::BIO_READ;
    case 'w':
    case 'W':
    case 'm':
    case 'M':
      write_count++;

      return BIL::BIO_WRITE;
    case 'f':
    case 'F':
      return BIL::BIO_FLUSH;
    case 't':
    case 'T':
    case 'd':
    case 'D':
      return BIL::BIO_TRIM;
    case 'c':
    case 'C':
      compute_count++;

      return BIL::BIO_READ_COMPUTE;
  }

  return BIL::BIO_NUM;
}

void TraceReplayer::parseLine() {
  std::string line;
  std::smatch match;

  // Read line
  while (true) {
    bool eof = false;

    {
      std::lock_guard<std::mutex> guard(m);

      eof = file.eof();
      std::getline(file, line);
    }

    if (eof) {
      reserveTermination = true;

      if (io_depth == 0) {
        // No on-the-fly I/O
        endCallback();
      }

      return;
    }
    if (std::regex_match(line, match, regex)) {
      break;
    }
  }

  // Get time
  linedata.tick = mergeTime(match);
  std::string operation = match[groupID[ID_OPERATION]].str();
  linedata.type = getType(operation);
  linedata.csdMatrixPreload = operation[0] == 'm' || operation[0] == 'M';

  if (linedata.type == BIL::BIO_READ_COMPUTE ||
      linedata.csdMatrixPreload) {
    uint64_t offset;
    uint64_t rows = parseInteger(match, groupID[ID_ROWS], "Rows");
    uint64_t cols = parseInteger(match, groupID[ID_COLS], "Cols");
    CSDWorkload::Shape shape =
        CSDWorkload::validateShape(rows, cols, "Trace replayer");

    linedata.matrixSLBA =
        parseInteger(match, groupID[ID_MATRIX_SLBA], "MatrixSLBA");
    linedata.rows = shape.rows;
    linedata.cols = shape.cols;
    linedata.matrixBytes = shape.matrixBytes;
    linedata.matrixTransferBytes = CSDWorkload::transferBytes(
        shape.matrixBytes, (uint64_t)lbaSize, "Trace replayer");
    linedata.matrixTransferLBAs = CSDWorkload::transferLBAs(
        shape.matrixBytes, (uint64_t)lbaSize, "Trace replayer");
    CSDWorkload::validatePlacement(linedata.matrixSLBA,
                                   linedata.matrixTransferLBAs, 1,
                                   ssdSize / lbaSize, "Trace replayer");
    CSDWorkload::validateControlBytes(linedata.rows, linedata.cols, 0,
                                      "Trace replayer");

    if (!CSDWorkload::checkedMul(linedata.matrixSLBA, (uint64_t)lbaSize,
                                 offset)) {
      SimpleSSD::panic("Trace replayer: CSD matrix offset overflow");
    }

    linedata.offset = offset;
    linedata.length = linedata.csdMatrixPreload ? linedata.matrixTransferBytes
                                                : linedata.matrixBytes;

    if (linedata.csdMatrixPreload) {
      linedata.matrixSeed =
          parseInteger(match, groupID[ID_MATRIX_SEED], "MatrixSeed");
    }
    else {
      linedata.vectorSeed =
          parseInteger(match, groupID[ID_VECTOR_SEED], "VectorSeed");
    }
  }
  else {
    // Fill BIO
    if (useLBAOffset) {
      linedata.offset = parseInteger(match, groupID[ID_LBA_OFFSET],
                                     "LBAOffset") *
                        lbaSize;
    }
    else {
      linedata.offset =
          parseInteger(match, groupID[ID_BYTE_OFFSET], "ByteOffset");
    }

    if (useLBALength) {
      linedata.length = parseInteger(match, groupID[ID_LBA_LENGTH],
                                     "LBALength") *
                        lbaSize;
    }
    else {
      linedata.length =
          parseInteger(match, groupID[ID_BYTE_LENGTH], "ByteLength");
    }
  }
}

void TraceReplayer::submitIO() {
  BIL::BIO bio;

  if (linedata.type == BIL::BIO_NUM) {
    SimpleSSD::panic("Unexpected request type.");
  }

  bio.callback = completionEvent;
  bio.id = io_count;
  bio.type = linedata.type;
  bio.offset = linedata.offset;
  bio.length = linedata.length;

  if (linedata.csdMatrixPreload) {
    MatrixInfo info;

    info.matrixSLBA = linedata.matrixSLBA;
    info.rows = linedata.rows;
    info.cols = linedata.cols;
    info.matrixBytes = linedata.matrixBytes;
    info.transferBytes = linedata.matrixTransferBytes;
    info.transferLBAs = linedata.matrixTransferLBAs;
    info.seed = linedata.matrixSeed;
    info.payload = CSDWorkload::makeMatrixPayload(
        linedata.rows, linedata.cols, linedata.matrixTransferBytes,
        linedata.matrixSeed);

    bio.payload = info.payload;
    pendingMatrixPreloads[bio.id] = info;
  }
  else if (bio.type == BIL::BIO_READ_COMPUTE) {
    bio.csd = std::make_shared<BIL::CSDGEMVRequest>();
    bio.csd->matrixSLBA = linedata.matrixSLBA;
    bio.csd->rows = linedata.rows;
    bio.csd->cols = linedata.cols;
    bio.csd->lda = linedata.cols;
    bio.csd->vectorFP16 =
        CSDWorkload::makeVector(linedata.cols, linedata.vectorSeed);
    bio.csd->opcode = csdOpcode;
    bio.csd->useSGL = csdUseSGL;

    if (csdVerifyOutput) {
      auto matrix = matrices.find(linedata.matrixSLBA);

      if (matrix == matrices.end()) {
        SimpleSSD::panic(
            "CSD trace verification requires a prior matching M preload");
      }
      if (matrix->second.rows != linedata.rows ||
          matrix->second.cols != linedata.cols) {
        SimpleSSD::panic(
            "CSD trace read_compute dimensions do not match matrix preload");
      }

      bio.csd->expectedFP32 = CSDWorkload::referenceGEMV(
          *matrix->second.payload, linedata.rows, linedata.cols,
          bio.csd->vectorFP16);
      bio.csd->verifyOutput = true;
    }

    pendingCSD[bio.id] = bio.csd;
  }

  io_submitted += bio.length;
  bioEntry.submitIO(bio);

  io_depth++;

  if ((max_io != 0 && io_count >= max_io)) {
    reserveTermination = true;

    return;
  }

  if (linedata.csdMatrixPreload) {
    nextIOIsSync = true;

    return;
  }

  parseLine();

  if (reserveTermination) {
    return;
  }

  switch (mode) {
    case MODE_STRICT:
      engine.scheduleEvent(submitEvent, linedata.tick - firstTick + initTime);
      break;
    case MODE_ASYNC:
      rescheduleSubmit(submissionLatency);
      break;
    default:
      break;
  }
}

void TraceReplayer::iocallback(uint64_t id, uint16_t status) {
  io_depth--;

  bool completedMatrixPreload = false;
  auto matrixPreload = pendingMatrixPreloads.find(id);
  auto pendingCompute = pendingCSD.find(id);

  if (status != 0) {
    if (matrixPreload != pendingMatrixPreloads.end()) {
      pendingMatrixPreloads.erase(matrixPreload);
      SimpleSSD::panic("ordinary write failed with NVMe status 0x%04X",
                       status);
    }
    if (pendingCompute != pendingCSD.end()) {
      failed_compute_count++;
      pendingCSD.erase(pendingCompute);
      SimpleSSD::panic("read_compute failed with NVMe status 0x%04X", status);
    }

    SimpleSSD::panic("ordinary I/O failed with NVMe status 0x%04X", status);
  }

  if (matrixPreload != pendingMatrixPreloads.end()) {
    matrices[matrixPreload->second.matrixSLBA] = matrixPreload->second;
    pendingMatrixPreloads.erase(matrixPreload);
    completedMatrixPreload = true;
  }

  if (pendingCompute != pendingCSD.end()) {
    std::string message;

    if (pendingCompute->second->verifyOutput &&
        !CSDWorkload::outputMatches(pendingCompute->second->outputFP32,
                                    pendingCompute->second->expectedFP32,
                                    &message)) {
      failed_compute_count++;
      pendingCSD.erase(pendingCompute);
      SimpleSSD::panic("read_compute GEMV verification failed: %s",
                       message.c_str());
    }

    if (pendingCompute->second->verifyOutput) {
      verified_compute_count++;
    }
    pendingCSD.erase(pendingCompute);
  }

  if (completedMatrixPreload && !reserveTermination) {
    parseLine();

    if (reserveTermination) {
      return;
    }
    if (mode == MODE_STRICT) {
      engine.scheduleEvent(submitEvent, linedata.tick - firstTick + initTime);

      return;
    }
  }

  if (reserveTermination) {
    // Everything is done
    if (io_depth == 0) {
      endCallback();
    }
  }

  if (mode == MODE_SYNC || nextIOIsSync) {
    // MODE_ASYNC submission blocked by I/O depth limitation
    // Let's submit here
    nextIOIsSync = false;

    rescheduleSubmit(submissionLatency + completionLatency);
  }
}

void TraceReplayer::rescheduleSubmit(uint64_t breakTime) {
  if (mode == MODE_ASYNC) {
    if (io_depth >= maxQueueDepth) {
      nextIOIsSync = true;

      return;
    }
  }
  else if (mode == MODE_STRICT) {
    return;
  }

  engine.scheduleEvent(submitEvent, engine.getCurrentTick() + breakTime);
}

}  // namespace IGL
