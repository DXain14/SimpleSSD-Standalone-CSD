if (NOT DEFINED PROGRAM OR NOT DEFINED CONFIG OR NOT DEFINED EXPECTED_REGEX)
  message(FATAL_ERROR "PROGRAM, CONFIG, and EXPECTED_REGEX are required")
endif ()

if (NOT DEFINED TIMEOUT)
  set(TIMEOUT 90)
endif ()

execute_process(
  COMMAND "${PROGRAM}" "${CONFIG}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT ${TIMEOUT}
)

if ("${result}" STREQUAL "0")
  message(STATUS "${stdout}")
  message(STATUS "${stderr}")
  message(FATAL_ERROR "CSD negative command unexpectedly succeeded")
endif ()

set(log "${stdout}\n${stderr}")
if (NOT log MATCHES "${EXPECTED_REGEX}")
  message(STATUS "${log}")
  message(FATAL_ERROR "CSD negative command did not report expected error")
endif ()

message(STATUS "CSD negative command failed as expected")
