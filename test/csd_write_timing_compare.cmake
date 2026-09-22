if (NOT PROGRAM)
  message(FATAL_ERROR "PROGRAM is required")
endif ()
if (NOT DISABLED_CONFIG)
  message(FATAL_ERROR "DISABLED_CONFIG is required")
endif ()
if (NOT ENABLED_CONFIG)
  message(FATAL_ERROR "ENABLED_CONFIG is required")
endif ()

execute_process(
  COMMAND "${PROGRAM}" "${DISABLED_CONFIG}"
  RESULT_VARIABLE disabled_result
  OUTPUT_VARIABLE disabled_output
  ERROR_VARIABLE disabled_error
)
if (NOT disabled_result EQUAL 0)
  message(FATAL_ERROR
    "CSD-disabled write timing run failed\n${disabled_output}${disabled_error}")
endif ()

execute_process(
  COMMAND "${PROGRAM}" "${ENABLED_CONFIG}"
  RESULT_VARIABLE enabled_result
  OUTPUT_VARIABLE enabled_output
  ERROR_VARIABLE enabled_error
)
if (NOT enabled_result EQUAL 0)
  message(FATAL_ERROR
    "CSD-enabled write timing run failed\n${enabled_output}${enabled_error}")
endif ()

if (NOT disabled_output MATCHES "WriteCompletionDelta: ([0-9]+)")
  message(FATAL_ERROR
    "CSD-disabled write timing output did not contain a delta\n"
    "${disabled_output}${disabled_error}")
endif ()
set(disabled_delta "${CMAKE_MATCH_1}")

if (NOT enabled_output MATCHES "WriteCompletionDelta: ([0-9]+)")
  message(FATAL_ERROR
    "CSD-enabled write timing output did not contain a delta\n"
    "${enabled_output}${enabled_error}")
endif ()
set(enabled_delta "${CMAKE_MATCH_1}")

if (NOT enabled_delta GREATER disabled_delta)
  message(FATAL_ERROR
    "CSD write timing regression was not observed as expected\n"
    "disabled=${disabled_delta}\n"
    "enabled=${enabled_delta}\n"
    "disabled log:\n${disabled_output}${disabled_error}\n"
    "enabled log:\n${enabled_output}${enabled_error}")
endif ()

message(STATUS
  "CSD write timing overlap passed: disabled=${disabled_delta}, "
  "enabled=${enabled_delta}")
