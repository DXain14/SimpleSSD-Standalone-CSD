if (NOT DEFINED PROGRAM OR NOT DEFINED WORKLOAD OR NOT DEFINED SSD_CONFIG OR
    NOT DEFINED ROOT OR NOT DEFINED EXPECTED_REGEX)
  message(FATAL_ERROR
    "PROGRAM, WORKLOAD, SSD_CONFIG, ROOT, and EXPECTED_REGEX are required")
endif ()

if (NOT DEFINED TIMEOUT)
  set(TIMEOUT 90)
endif ()

execute_process(
  COMMAND "${PROGRAM}" "${WORKLOAD}" "${SSD_CONFIG}" .
  WORKING_DIRECTORY "${ROOT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT ${TIMEOUT}
)

if ("${result}" STREQUAL "0")
  message(STATUS "${output}")
  message(STATUS "${error}")
  message(FATAL_ERROR "CSD negative workload unexpectedly succeeded")
endif ()

set(combined "${output}\n${error}")
if (NOT combined MATCHES "${EXPECTED_REGEX}")
  message(STATUS "${combined}")
  message(FATAL_ERROR
    "CSD negative workload did not report the expected error")
endif ()

message(STATUS "CSD negative workload failed as expected")
