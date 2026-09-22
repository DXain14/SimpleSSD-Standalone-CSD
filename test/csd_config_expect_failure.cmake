if (NOT DEFINED PROGRAM OR NOT DEFINED BASE_CONFIG OR NOT DEFINED CONFIG OR
    NOT DEFINED REPLACE_FROM OR NOT DEFINED REPLACE_TO OR
    NOT DEFINED EXPECTED_REGEX)
  message(FATAL_ERROR
    "PROGRAM, BASE_CONFIG, CONFIG, REPLACE_FROM, REPLACE_TO, and "
    "EXPECTED_REGEX are required")
endif ()

if (NOT DEFINED TIMEOUT)
  set(TIMEOUT 90)
endif ()

file(READ "${BASE_CONFIG}" content)
set(original "${content}")
string(REPLACE "${REPLACE_FROM}" "${REPLACE_TO}" content "${content}")

if ("${content}" STREQUAL "${original}")
  message(FATAL_ERROR "CSD generated config did not replace '${REPLACE_FROM}'")
endif ()

get_filename_component(config_dir "${CONFIG}" DIRECTORY)
file(MAKE_DIRECTORY "${config_dir}")
file(WRITE "${CONFIG}" "${content}")

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
  message(FATAL_ERROR "CSD invalid configuration unexpectedly succeeded")
endif ()

set(log "${stdout}\n${stderr}")
if (NOT log MATCHES "${EXPECTED_REGEX}")
  message(STATUS "${log}")
  message(FATAL_ERROR
    "CSD invalid configuration did not report the expected error")
endif ()

message(STATUS "CSD invalid configuration failed as expected")
