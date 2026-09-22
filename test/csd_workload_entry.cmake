if (NOT DEFINED PROGRAM)
  message(FATAL_ERROR "PROGRAM is required")
endif ()

if (NOT DEFINED WORKLOAD)
  message(FATAL_ERROR "WORKLOAD is required")
endif ()

if (NOT DEFINED SSD_CONFIG)
  message(FATAL_ERROR "SSD_CONFIG is required")
endif ()

if (NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif ()

execute_process(
  COMMAND ${PROGRAM} ${WORKLOAD} ${SSD_CONFIG} .
  WORKING_DIRECTORY ${ROOT}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 90
)

if (NOT result EQUAL 0)
  message(STATUS "${output}")
  message(STATUS "${error}")
  message(FATAL_ERROR "Standalone CSD workload failed")
endif ()

set(combined "${output}\n${error}")

if (NOT combined MATCHES "csd\\.pu\\.command\\.count[ \t]+[1-9][0-9]*\\.")
  message(STATUS "${combined}")
  message(FATAL_ERROR "CSD workload did not submit read_compute to the PU")
endif ()

if (NOT combined MATCHES "ReadCompute: [1-9][0-9]*")
  message(STATUS "${combined}")
  message(FATAL_ERROR "IGL/BIL workload statistics did not count read_compute")
endif ()

if (NOT combined MATCHES "VerifiedReadCompute: [1-9][0-9]*")
  message(STATUS "${combined}")
  message(FATAL_ERROR "CSD workload did not verify read_compute output")
endif ()
