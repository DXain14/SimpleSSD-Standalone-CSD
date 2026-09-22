if (NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif ()

set(embedded "${ROOT}/simplessd")
set(test_root "${ROOT}/test")

# A clean standalone clone has only its own submodule. PRIMARY_ROOT is optional
# and enables the stronger outer-tree mirror comparison in the development
# workspace without making the public repository depend on a sibling checkout.
if (DEFINED PRIMARY_ROOT AND NOT PRIMARY_ROOT STREQUAL "")
  set(primary "${PRIMARY_ROOT}")
else ()
  set(primary "${embedded}")
endif ()

function(require_contains file pattern)
  file(READ "${file}" content)
  string(FIND "${content}" "${pattern}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "${file} does not contain required path marker: ${pattern}")
  endif ()
endfunction ()

function(require_absent file pattern)
  file(READ "${file}" content)
  string(FIND "${content}" "${pattern}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "${file} contains forbidden bypass marker: ${pattern}")
  endif ()
endfunction ()

function(require_same_file relative)
  set(primary_file "${primary}/${relative}")
  set(embedded_file "${embedded}/${relative}")

  if (NOT EXISTS "${primary_file}")
    message(FATAL_ERROR "primary SimpleSSD is missing ${relative}")
  endif ()
  if (NOT EXISTS "${embedded_file}")
    message(FATAL_ERROR "embedded SimpleSSD is missing ${relative}")
  endif ()

  file(SHA256 "${primary_file}" primary_hash)
  file(SHA256 "${embedded_file}" embedded_hash)
  if (NOT primary_hash STREQUAL embedded_hash)
    message(FATAL_ERROR
      "CSD critical path mirror differs for ${relative}\n"
      "primary: ${primary_file}\n"
      "embedded: ${embedded_file}")
  endif ()
endfunction ()

file(GLOB_RECURSE primary_files RELATIVE "${primary}/csd" "${primary}/csd/*")
foreach (relative IN LISTS primary_files)
  set(primary_file "${primary}/csd/${relative}")
  set(embedded_file "${embedded}/csd/${relative}")

  if (IS_DIRECTORY "${primary_file}")
    continue()
  endif ()
  if (NOT EXISTS "${embedded_file}")
    message(FATAL_ERROR "embedded SimpleSSD is missing CSD file ${relative}")
  endif ()

  file(SHA256 "${primary_file}" primary_hash)
  file(SHA256 "${embedded_file}" embedded_hash)
  if (NOT primary_hash STREQUAL embedded_hash)
    message(FATAL_ERROR "CSD mirror differs for ${relative}")
  endif ()
endforeach ()

file(GLOB_RECURSE embedded_files RELATIVE "${embedded}/csd" "${embedded}/csd/*")
foreach (relative IN LISTS embedded_files)
  if (NOT EXISTS "${primary}/csd/${relative}")
    message(FATAL_ERROR "embedded SimpleSSD has unmatched CSD file ${relative}")
  endif ()
endforeach ()

set(CSD_MIRRORED_FILES
  CMakeLists.txt
  config/sample.cfg
  sim/config_reader.hh
  sim/config_reader.cc
  util/def.hh
  util/def.cc
  ftl/abstract_ftl.hh
  ftl/ftl.hh
  ftl/ftl.cc
  ftl/page_mapping.hh
  ftl/page_mapping.cc
  icl/icl.hh
  icl/icl.cc
  icl/generic_cache.cc
  hil/hil.hh
  hil/hil.cc
  hil/nvme/def.hh
  hil/nvme/dma.cc
  hil/nvme/controller.hh
  hil/nvme/controller.cc
  hil/nvme/namespace.hh
  hil/nvme/namespace.cc
  hil/nvme/subsystem.hh
  hil/nvme/subsystem.cc
  pal/pal_old.hh
  pal/pal_old.cc
)

foreach (relative IN LISTS CSD_MIRRORED_FILES)
  require_same_file("${relative}")
endforeach ()

require_contains("${embedded}/CMakeLists.txt" "csd/pu.cc")
require_contains("${embedded}/sim/config_reader.hh" "CONFIG_CSD")
require_contains("${embedded}/sim/config_reader.hh" "CSD::Config csdConfig")
require_contains("${embedded}/sim/config_reader.cc" "SECTION_CSD")
require_contains("${embedded}/sim/config_reader.cc" "csdConfig.update")
require_contains("${embedded}/sim/config_reader.cc" "csdConfig.setConfig")
require_contains("${embedded}/hil/nvme/subsystem.cc" "pPU->submit")
require_contains("${embedded}/hil/nvme/subsystem.cc" "readPayload")
require_contains("${embedded}/hil/nvme/subsystem.cc" "csd.pu.")
require_contains("${embedded}/hil/hil.cc" "pICL->readPayload")
require_contains("${embedded}/icl/icl.cc" "pFTL->readPayload")
require_contains("${embedded}/ftl/page_mapping.cc" "pArrayStore->read")
require_contains("${embedded}/ftl/page_mapping.cc" "pArrayStore->copy")
require_contains("${embedded}/ftl/page_mapping.cc" "pArrayStore->erase")
require_contains("${embedded}/ftl/page_mapping.cc" "pPAL->read")
require_contains("${embedded}/ftl/page_mapping.cc" "writePayloadInternal")

foreach (test_file IN ITEMS csd_smoke.cc csd_integration.cc csd_stress.cc
                             csd_disabled.cc csd_write_timing.cc)
  require_absent("${test_root}/${test_file}" "PU::submit")
  require_absent("${test_root}/${test_file}" "FlashArrayStore")
  require_absent("${test_root}/${test_file}" "MemDisk")
  require_absent("${test_root}/${test_file}" "readPayload")
endforeach ()

message(STATUS "CSD source mirror and NVMe-path contract passed")
