if (NOT DEFINED PROGRAM OR NOT DEFINED SLOW_CONFIG OR NOT DEFINED FAST_CONFIG OR
    NOT DEFINED METRIC)
  message(FATAL_ERROR "PROGRAM, SLOW_CONFIG, FAST_CONFIG, and METRIC are required")
endif ()

function(read_metric config output)
  execute_process(
    COMMAND "${PROGRAM}" "${config}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )

  if (NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "integration run failed for ${config}:\n${stdout}\n${stderr}")
  endif ()

  string(REGEX MATCH "${METRIC}=([0-9]+)" match "${stdout}")
  if (NOT match)
    message(FATAL_ERROR "did not find ${METRIC} timing snapshot in:\n${stdout}")
  endif ()

  set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction ()

read_metric("${SLOW_CONFIG}" slow)
read_metric("${FAST_CONFIG}" fast)

if (fast GREATER_EQUAL slow)
  message(FATAL_ERROR
    "${METRIC} did not decrease: slow=${slow}, fast=${fast}")
endif ()

message(STATUS "${METRIC} timing scaling passed: ${slow} -> ${fast}")
