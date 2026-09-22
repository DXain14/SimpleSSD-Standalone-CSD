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

#include "igl/request/request_config.hh"

#include "simplessd/sim/trace.hh"
#include "util/convert.hh"

namespace IGL {

const char NAME_IO_SIZE[] = "io_size";
const char NAME_IO_TYPE[] = "readwrite";
const char NAME_IO_MIX_RATIO[] = "rwmixread";
const char NAME_BLOCK_SIZE[] = "blocksize";
const char NAME_BLOCK_ALIGN[] = "blockalign";
const char NAME_IO_MODE[] = "iomode";
const char NAME_IO_DEPTH[] = "iodepth";
const char NAME_OFFSET[] = "offset";
const char NAME_SIZE[] = "size";
const char NAME_THINKTIME[] = "thinktime";
const char NAME_RANDOM_SEED[] = "randseed";
const char NAME_TIME_BASED[] = "time_based";
const char NAME_RUN_TIME[] = "runtime";
const char NAME_CSD_MATRIX_SLBA[] = "csd_matrix_slba";
const char NAME_CSD_ROWS[] = "csd_rows";
const char NAME_CSD_COLS[] = "csd_cols";
const char NAME_CSD_MATRIX_COUNT[] = "csd_matrix_count";
const char NAME_CSD_VECTOR_SEED[] = "csd_vector_seed";
const char NAME_CSD_OPCODE[] = "csd_opcode";
const char NAME_CSD_USE_SGL[] = "csd_use_sgl";
const char NAME_CSD_PREWRITE_MATRIX[] = "csd_prewrite_matrix";
const char NAME_CSD_VERIFY_OUTPUT[] = "csd_verify_output";

RequestConfig::RequestConfig() {
  io_size = 0;
  type = IO_READ;
  rwmixread = 0;
  blocksize = 0;
  blockalign = 0;
  mode = IO_SYNC;
  iodepth = 0;
  offset = 0;
  size = 0;
  thinktime = 0;
  randseed = 0;
  time_based = false;
  runtime = 0;
  csdMatrixSLBA = 0;
  csdRows = 16;
  csdCols = 16;
  csdMatrixCount = 1;
  csdVectorSeed = 1;
  csdOpcode = 0xC0;
  csdUseSGL = false;
  csdPrewriteMatrix = true;
  csdVerifyOutput = false;
}

bool RequestConfig::setConfig(const char *name, const char *value) {
  bool ret = true;

  if (MATCH_NAME(NAME_IO_SIZE)) {
    io_size = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_IO_TYPE)) {
    if (strcasecmp(value, "read") == 0) {
      type = IO_READ;
    }
    else if (strcasecmp(value, "write") == 0) {
      type = IO_WRITE;
    }
    else if (strcasecmp(value, "randread") == 0) {
      type = IO_RANDREAD;
    }
    else if (strcasecmp(value, "randwrite") == 0) {
      type = IO_RANDWRITE;
    }
    else if (strcasecmp(value, "readwrite") == 0) {
      type = IO_READWRITE;
    }
    else if (strcasecmp(value, "randrw") == 0) {
      type = IO_RANDRW;
    }
    else if (strcasecmp(value, "readcompute") == 0) {
      type = IO_READCOMPUTE;
    }
    else if (strcasecmp(value, "randreadcompute") == 0) {
      type = IO_RANDREADCOMPUTE;
    }
    else {
      type = IO_TYPE_NUM;
    }
  }
  else if (MATCH_NAME(NAME_IO_MIX_RATIO)) {
    rwmixread = strtof(value, nullptr);
  }
  else if (MATCH_NAME(NAME_BLOCK_SIZE)) {
    blocksize = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_BLOCK_ALIGN)) {
    blockalign = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_IO_MODE)) {
    if (strcasecmp(value, "sync") == 0) {
      mode = IO_SYNC;
    }
    else if (strcasecmp(value, "async") == 0) {
      mode = IO_ASYNC;
    }
    else {
      mode = IO_MODE_NUM;
    }
  }
  else if (MATCH_NAME(NAME_IO_DEPTH)) {
    iodepth = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_OFFSET)) {
    offset = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_SIZE)) {
    size = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_THINKTIME)) {
    thinktime = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_RANDOM_SEED)) {
    randseed = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_TIME_BASED)) {
    time_based = convertBoolean(value);
  }
  else if (MATCH_NAME(NAME_RUN_TIME)) {
    runtime = convertTime(value);
  }
  else if (MATCH_NAME(NAME_CSD_MATRIX_SLBA)) {
    csdMatrixSLBA = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_CSD_ROWS)) {
    csdRows = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_CSD_COLS)) {
    csdCols = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_CSD_MATRIX_COUNT)) {
    csdMatrixCount = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_CSD_VECTOR_SEED)) {
    csdVectorSeed = convertInteger(value);
  }
  else if (MATCH_NAME(NAME_CSD_OPCODE)) {
    csdOpcode = strtoul(value, nullptr, 0);
  }
  else if (MATCH_NAME(NAME_CSD_USE_SGL)) {
    csdUseSGL = convertBoolean(value);
  }
  else if (MATCH_NAME(NAME_CSD_PREWRITE_MATRIX)) {
    csdPrewriteMatrix = convertBoolean(value);
  }
  else if (MATCH_NAME(NAME_CSD_VERIFY_OUTPUT)) {
    csdVerifyOutput = convertBoolean(value);
  }
  else {
    ret = false;
  }

  return ret;
}

void RequestConfig::update() {
  if (type == IO_TYPE_NUM) {
    SimpleSSD::panic("Invalid value of readwrite");
  }
  if (mode == IO_MODE_NUM) {
    SimpleSSD::panic("Invalid value of iomode");
  }
  if (rwmixread < 0 || rwmixread > 1) {
    SimpleSSD::panic("Invalid value of rwmixread");
  }
  if ((type == IO_READCOMPUTE || type == IO_RANDREADCOMPUTE) &&
      (csdRows == 0 || csdCols == 0 || csdMatrixCount == 0 ||
       csdOpcode > 0xFF)) {
    SimpleSSD::panic("Invalid CSD read_compute generator configuration");
  }
}

uint64_t RequestConfig::readUint(uint32_t idx) {
  uint64_t ret = 0;

  switch (idx) {
    case REQUEST_IO_SIZE:
      ret = io_size;
      break;
    case REQUEST_IO_TYPE:
      ret = type;
      break;
    case REQUEST_BLOCK_SIZE:
      ret = blocksize;
      break;
    case REQUEST_BLOCK_ALIGN:
      ret = blockalign;
      break;
    case REQUEST_IO_MODE:
      ret = mode;
      break;
    case REQUEST_IO_DEPTH:
      ret = iodepth;
      break;
    case REQUEST_OFFSET:
      ret = offset;
      break;
    case REQUEST_SIZE:
      ret = size;
      break;
    case REQUEST_THINKTIME:
      ret = thinktime;
      break;
    case REQUEST_RANDOM_SEED:
      ret = randseed;
      break;
    case REQUEST_RUN_TIME:
      ret = runtime;
      break;
    case REQUEST_CSD_MATRIX_SLBA:
      ret = csdMatrixSLBA;
      break;
    case REQUEST_CSD_ROWS:
      ret = csdRows;
      break;
    case REQUEST_CSD_COLS:
      ret = csdCols;
      break;
    case REQUEST_CSD_MATRIX_COUNT:
      ret = csdMatrixCount;
      break;
    case REQUEST_CSD_VECTOR_SEED:
      ret = csdVectorSeed;
      break;
    case REQUEST_CSD_OPCODE:
      ret = csdOpcode;
      break;
  }

  return ret;
}

float RequestConfig::readFloat(uint32_t idx) {
  float ret = 0.f;

  switch (idx) {
    case REQUEST_IO_MIX_RATIO:
      ret = rwmixread;
      break;
  }

  return ret;
}

bool RequestConfig::readBoolean(uint32_t idx) {
  bool ret = false;

  switch (idx) {
    case REQUEST_TIME_BASED:
      ret = time_based;
      break;
    case REQUEST_CSD_USE_SGL:
      ret = csdUseSGL;
      break;
    case REQUEST_CSD_PREWRITE_MATRIX:
      ret = csdPrewriteMatrix;
      break;
    case REQUEST_CSD_VERIFY_OUTPUT:
      ret = csdVerifyOutput;
      break;
  }

  return ret;
}

}  // namespace IGL
