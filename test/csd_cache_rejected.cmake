if (NOT DEFINED PROGRAM OR NOT DEFINED CONFIG)
  message(FATAL_ERROR "PROGRAM and CONFIG are required")
endif ()

execute_process(
  COMMAND "${PROGRAM}" "${CONFIG}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if ("${result}" STREQUAL "0")
  message(FATAL_ERROR "CSD with ICL cache enabled unexpectedly initialized")
endif ()

set(log "${stdout}\n${stderr}")
if (NOT log MATCHES "CSD requires EnableReadCache=0 and EnableWriteCache=0")
  message(FATAL_ERROR "cache rejection did not report the CSD cache contract:\n${log}")
endif ()

message(STATUS "CSD cache-off configuration rejection passed")
