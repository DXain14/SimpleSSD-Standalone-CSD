# Copyright (C) 2017 CAMELab
#
# This file is part of SimpleSSD.
#
# SimpleSSD is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# SimpleSSD is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.

# Set version and project name
cmake_minimum_required(VERSION 3.10)
project(simplessd-standalone)

# Add options for debug build
option(DEBUG_BUILD "Build SimpleSSD-Standalone in debug mode." OFF)
option(CSD_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer." OFF)

# Set DRAMPower path
set(DRAMPOWER_SOURCE_DIR
  "${PROJECT_SOURCE_DIR}/lib/drampower/src"
  CACHE PATH
  ""
  FORCE
)

# Check SimpleSSD is valid
if (NOT EXISTS "${PROJECT_SOURCE_DIR}/simplessd/CMakeLists.txt")
  message(FATAL_ERROR "Failed to find SimpleSSD. \
Check you initialized submodules.")
endif ()

# Add subproject
add_subdirectory(${PROJECT_SOURCE_DIR}/simplessd
  ${CMAKE_BINARY_DIR}/simplessd
)

# Set include directories
include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/simplessd
  ${DRAMPOWER_SOURCE_DIR}
)

# Platform specific settings
if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/wd4819)  # Surpress unicode warning
  add_compile_options(/wd4305)  # Surpress truncation warning
else ()
  if (DEBUG_BUILD)
    set(CMAKE_CXX_FLAGS
      "-O0 -g -rdynamic -pthread -Wall -Wextra -Werror ${CMAKE_CXX_FLAGS}")
  else ()
    set(CMAKE_CXX_FLAGS
      "-O2 -rdynamic -pthread -Wall -Wextra -Werror ${CMAKE_CXX_FLAGS}")
  endif ()

  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9")
      message(FATAL_ERROR "GNU g++ < 4.9 does not support <regex>.\
Please update your compiler.")
    endif ()
  endif ()

  if (CSD_SANITIZERS)
    set(CMAKE_CXX_FLAGS
      "-fsanitize=address,undefined -fno-omit-frame-pointer ${CMAKE_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS
      "-fsanitize=address,undefined ${CMAKE_EXE_LINKER_FLAGS}")
  endif ()
endif ()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Specify source files
set(SRC_BIL
  bil/entry.cc
  bil/noop_scheduler.cc
)
set(SRC_IGL_REQUEST
  igl/csd_workload.cc
  igl/request/request_config.cc
  igl/request/request_generator.cc
)
set(SRC_IGL_TRACE
  igl/trace/trace_config.cc
  igl/trace/trace_replayer.cc
)
set(SRC_LIB_DRAMPOWER
  lib/drampower/src/CAHelpers.cc
  lib/drampower/src/CmdHandlers.cc
  lib/drampower/src/CmdScheduler.cc
  lib/drampower/src/CommandAnalysis.cc
  lib/drampower/src/MemArchitectureSpec.cc
  lib/drampower/src/MemBankWiseParams.cc
  lib/drampower/src/MemCommand.cc
  lib/drampower/src/MemoryPowerModel.cc
  lib/drampower/src/MemorySpecification.cc
  lib/drampower/src/MemPowerSpec.cc
  lib/drampower/src/MemTimingSpec.cc
  lib/drampower/src/Parameter.cc
  lib/drampower/src/Parametrisable.cc
  lib/drampower/src/TraceParser.cc
  lib/drampower/src/libdrampower/LibDRAMPower.cc
)
set(SRC_SIL_NONE
  sil/none/none.cc
)
set(SRC_SIL_NVME
  sil/nvme/nvme.cc
  sil/nvme/prp.cc
  sil/nvme/queue.cc
)
set(SRC_SIM
  sim/cfg_reader.cc
  sim/engine.cc
  sim/global_config.cc
  sim/main.cc
  sim/signal.cc
)
set(SRC_UTIL
  util/convert.cc
  util/print.cc
  util/stopwatch.cc
)

# Source group for MSVC
SOURCE_GROUP("Source Files\\bil" FILES ${SRC_BIL})
SOURCE_GROUP("Source Files\\igl\\request" FILES ${SRC_IGL_REQUEST})
SOURCE_GROUP("Source Files\\igl\\trace" FILES ${SRC_IGL_TRACE})
SOURCE_GROUP("Source Files\\lib\\drampower" FILES ${SRC_LIB_DRAMPOWER})
SOURCE_GROUP("Source Files\\sil\\none" FILES ${SRC_SIL_NONE})
SOURCE_GROUP("Source Files\\sil\\nvme" FILES ${SRC_SIL_NVME})
SOURCE_GROUP("Source Files\\sim" FILES ${SRC_SIM})
SOURCE_GROUP("Source Files\\util" FILES ${SRC_UTIL})

# Define executable
add_executable(simplessd-standalone
  ${SRC_BIL}
  ${SRC_IGL_REQUEST}
  ${SRC_IGL_TRACE}
  ${SRC_LIB_DRAMPOWER}
  ${SRC_SIL_NONE}
  ${SRC_SIL_NVME}
  ${SRC_SIM}
  ${SRC_UTIL}
)
target_link_libraries(simplessd-standalone simplessd)

set(SRC_CSD_TEST_SUPPORT
  ${SRC_LIB_DRAMPOWER}
  sil/nvme/nvme.cc
  sil/nvme/prp.cc
  sil/nvme/queue.cc
  sim/engine.cc
  util/stopwatch.cc
)

add_executable(simplessd-csd-smoke
  test/csd_smoke.cc
  ${SRC_CSD_TEST_SUPPORT}
)
target_link_libraries(simplessd-csd-smoke simplessd)

add_executable(simplessd-csd-unit
  test/csd_unit.cc
)
target_link_libraries(simplessd-csd-unit simplessd)

add_executable(simplessd-csd-integration
  test/csd_integration.cc
  ${SRC_CSD_TEST_SUPPORT}
)
target_link_libraries(simplessd-csd-integration simplessd)

add_executable(simplessd-csd-stress
  test/csd_stress.cc
  ${SRC_CSD_TEST_SUPPORT}
)
target_link_libraries(simplessd-csd-stress simplessd)

add_executable(simplessd-csd-disabled
  test/csd_disabled.cc
  ${SRC_CSD_TEST_SUPPORT}
)
target_link_libraries(simplessd-csd-disabled simplessd)

add_executable(simplessd-csd-write-timing
  test/csd_write_timing.cc
  ${SRC_CSD_TEST_SUPPORT}
)
target_link_libraries(simplessd-csd-write-timing simplessd)

add_custom_target(simplessd-csd-stress-full
  COMMAND simplessd-csd-stress ${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
  DEPENDS simplessd-csd-stress
)

include(CTest)

if (BUILD_TESTING)
  add_test(NAME csd_unit COMMAND simplessd-csd-unit)
  add_test(NAME csd_smoke
    COMMAND simplessd-csd-smoke ${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg)
  add_test(NAME csd_integration_random
    COMMAND simplessd-csd-integration
      ${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg)
  add_test(NAME csd_integration_no_random
    COMMAND simplessd-csd-integration
      ${PROJECT_SOURCE_DIR}/test/csd_enabled_no_random.cfg)
  add_test(NAME csd_integration_custom_opcode
    COMMAND simplessd-csd-integration
      ${PROJECT_SOURCE_DIR}/test/csd_enabled_opcode.cfg 0xC1)
  add_test(NAME csd_integration_opcode_ff
    COMMAND simplessd-csd-integration
      ${PROJECT_SOURCE_DIR}/test/csd_enabled_opcode_ff.cfg 0xFF)
  add_test(NAME csd_disabled
    COMMAND simplessd-csd-disabled ${PROJECT_SOURCE_DIR}/test/csd_disabled.cfg)
  add_test(NAME csd_disabled_cache_allowed
    COMMAND simplessd-csd-disabled
      ${PROJECT_SOURCE_DIR}/test/csd_disabled_cache_enabled.cfg)
  add_test(NAME csd_write_timing_overlap
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-write-timing>
      -DDISABLED_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_disabled.cfg
      -DENABLED_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -P ${PROJECT_SOURCE_DIR}/test/csd_write_timing_compare.cmake)
  add_test(NAME csd_timing_bandwidth
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-integration>
      -DSLOW_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DFAST_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_fast_bandwidth.cfg
      -DMETRIC=internal_read
      -P ${PROJECT_SOURCE_DIR}/test/csd_timing_scaling.cmake)
  add_test(NAME csd_timing_compute
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-integration>
      -DSLOW_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DFAST_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_fast_compute.cfg
      -DMETRIC=compute
      -P ${PROJECT_SOURCE_DIR}/test/csd_timing_scaling.cmake)
  add_test(NAME csd_cache_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DCONFIG=${PROJECT_SOURCE_DIR}/test/csd_cache_enabled.cfg
      -P ${PROJECT_SOURCE_DIR}/test/csd_cache_rejected.cmake)
  add_test(NAME csd_cache_override_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_cache_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_cache_override.cfg
      "-DREPLACE_FROM=RequireICLCacheOff = 1"
      "-DREPLACE_TO=RequireICLCacheOff = 0"
      -DEXPECTED_REGEX=CSD.requires.EnableReadCache=0.and.EnableWriteCache=0
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_write_cache_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_write_cache_enabled.cfg
      "-DREPLACE_FROM=EnableWriteCache = 0"
      "-DREPLACE_TO=EnableWriteCache = 1"
      -DEXPECTED_REGEX=CSD.requires.EnableReadCache=0.and.EnableWriteCache=0
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_invalid_opcode_read_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_invalid_opcode_read.cfg
      "-DREPLACE_FROM=ReadComputeOpcode = 0xC0"
      "-DREPLACE_TO=ReadComputeOpcode = 0x02"
      -DEXPECTED_REGEX=ReadComputeOpcode.should.be.vendor-specific.NVM.opcode.*0xC0-0xFF
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_invalid_opcode_bf_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_invalid_opcode_bf.cfg
      "-DREPLACE_FROM=ReadComputeOpcode = 0xC0"
      "-DREPLACE_TO=ReadComputeOpcode = 0xBF"
      -DEXPECTED_REGEX=ReadComputeOpcode.should.be.vendor-specific.NVM.opcode.*0xC0-0xFF
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_invalid_gflops_nan_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_invalid_gflops_nan.cfg
      "-DREPLACE_FROM=PUComputeGFLOPS = 0.001"
      "-DREPLACE_TO=PUComputeGFLOPS = nan"
      -DEXPECTED_REGEX=PUComputeGFLOPS.should.be.finite.and.positive
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_invalid_gflops_inf_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_invalid_gflops_inf.cfg
      "-DREPLACE_FROM=PUComputeGFLOPS = 0.001"
      "-DREPLACE_TO=PUComputeGFLOPS = inf"
      -DEXPECTED_REGEX=PUComputeGFLOPS.should.be.finite.and.positive
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_invalid_gflops_tiny_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_invalid_gflops_tiny.cfg
      "-DREPLACE_FROM=PUComputeGFLOPS = 0.001"
      "-DREPLACE_TO=PUComputeGFLOPS = 1e-30"
      -DEXPECTED_REGEX=PUComputeGFLOPS.is.too.small.for.MaxMatrixBytes
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_invalid_bandwidth_tiny_rejected
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DBASE_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DCONFIG=${PROJECT_BINARY_DIR}/csd_invalid_bandwidth_tiny.cfg
      "-DREPLACE_FROM=InternalReadBandwidth = 11200000000"
      "-DREPLACE_TO=InternalReadBandwidth = 1"
      -DEXPECTED_REGEX=InternalReadBandwidth.is.too.small.for.MaxMatrixBytes
      -P ${PROJECT_SOURCE_DIR}/test/csd_config_expect_failure.cmake)
  add_test(NAME csd_control_bytes_too_large
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-csd-smoke>
      -DCONFIG=${PROJECT_SOURCE_DIR}/test/csd_max_control_64.cfg
      "-DEXPECTED_REGEX=read_compute.failed:.0x4002"
      -P ${PROJECT_SOURCE_DIR}/test/csd_program_expect_failure.cmake)
  add_test(NAME csd_source_contract
    COMMAND ${CMAKE_COMMAND}
      -DROOT=${PROJECT_SOURCE_DIR}
      -P ${PROJECT_SOURCE_DIR}/test/csd_source_contract.cmake)
  add_test(NAME csd_generator_entry
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-standalone>
      -DWORKLOAD=${PROJECT_SOURCE_DIR}/test/csd_generator_workload.cfg
      -DSSD_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DROOT=${PROJECT_SOURCE_DIR}
      -P ${PROJECT_SOURCE_DIR}/test/csd_workload_entry.cmake)
  add_test(NAME csd_trace_entry
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-standalone>
      -DWORKLOAD=${PROJECT_SOURCE_DIR}/test/csd_trace_workload.cfg
      -DSSD_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DROOT=${PROJECT_SOURCE_DIR}
      -P ${PROJECT_SOURCE_DIR}/test/csd_workload_entry.cmake)
  add_test(NAME csd_trace_requires_nvme
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-standalone>
      -DWORKLOAD=${PROJECT_SOURCE_DIR}/test/csd_trace_requires_nvme.cfg
      -DSSD_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DROOT=${PROJECT_SOURCE_DIR}
      -DEXPECTED_REGEX=CSD.trace.read_compute.requires.Interface.=.1
      -P ${PROJECT_SOURCE_DIR}/test/csd_expect_failure.cmake)
  add_test(NAME csd_generator_invalid_dims
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-standalone>
      -DWORKLOAD=${PROJECT_SOURCE_DIR}/test/csd_generator_invalid_dims.cfg
      -DSSD_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DROOT=${PROJECT_SOURCE_DIR}
      -DEXPECTED_REGEX=invalid.CSD.GEMV.rows/cols
      -P ${PROJECT_SOURCE_DIR}/test/csd_expect_failure.cmake)
  add_test(NAME csd_trace_invalid_dims
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-standalone>
      -DWORKLOAD=${PROJECT_SOURCE_DIR}/test/csd_trace_invalid_dims.cfg
      -DSSD_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DROOT=${PROJECT_SOURCE_DIR}
      -DEXPECTED_REGEX=invalid.CSD.GEMV.rows/cols
      -P ${PROJECT_SOURCE_DIR}/test/csd_expect_failure.cmake)
  add_test(NAME csd_workload_status_propagation
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-standalone>
      -DWORKLOAD=${PROJECT_SOURCE_DIR}/test/csd_status_bad_opcode.cfg
      -DSSD_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DROOT=${PROJECT_SOURCE_DIR}
      -DEXPECTED_REGEX=read_compute.failed.with.NVMe.status
      -P ${PROJECT_SOURCE_DIR}/test/csd_expect_failure.cmake)
  add_test(NAME csd_trace_stress
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:simplessd-standalone>
      -DSSD_CONFIG=${PROJECT_SOURCE_DIR}/test/csd_enabled.cfg
      -DROOT=${PROJECT_SOURCE_DIR}
      -DBINARY_DIR=${PROJECT_BINARY_DIR}
      -P ${PROJECT_SOURCE_DIR}/test/csd_trace_stress.cmake)
  add_test(NAME csd_stress
    COMMAND simplessd-csd-stress ${PROJECT_SOURCE_DIR}/test/csd_gc.cfg
      20000 --require-payload-copy)
  add_test(NAME csd_gc_cost_benefit_stress
    COMMAND simplessd-csd-stress
      ${PROJECT_SOURCE_DIR}/test/csd_gc_cost_benefit.cfg
      20000 --require-payload-copy)

  set_tests_properties(csd_unit csd_smoke csd_integration_random
    csd_integration_no_random csd_integration_custom_opcode
    csd_integration_opcode_ff csd_disabled csd_disabled_cache_allowed
    csd_write_timing_overlap csd_timing_bandwidth csd_timing_compute
    csd_cache_rejected
    csd_cache_override_rejected csd_write_cache_rejected
    csd_invalid_opcode_read_rejected csd_invalid_opcode_bf_rejected
    csd_invalid_gflops_nan_rejected csd_invalid_gflops_inf_rejected
    csd_invalid_gflops_tiny_rejected
    csd_invalid_bandwidth_tiny_rejected
    csd_control_bytes_too_large
    csd_source_contract csd_generator_entry csd_trace_entry
    csd_trace_requires_nvme csd_generator_invalid_dims
    csd_trace_invalid_dims csd_workload_status_propagation
    PROPERTIES LABELS "csd")
  set_tests_properties(csd_trace_stress PROPERTIES LABELS "csd;stress"
    TIMEOUT 600)
  set_tests_properties(csd_stress PROPERTIES LABELS "csd;stress"
    TIMEOUT 240)
  set_tests_properties(csd_gc_cost_benefit_stress
    PROPERTIES LABELS "csd;stress" TIMEOUT 240)
endif ()
