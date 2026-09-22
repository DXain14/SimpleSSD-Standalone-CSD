if (NOT DEFINED PROGRAM OR NOT DEFINED SSD_CONFIG OR NOT DEFINED ROOT OR
    NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "PROGRAM, SSD_CONFIG, ROOT, and BINARY_DIR are required")
endif ()

set(trace "${BINARY_DIR}/csd_trace_stress.txt")
set(workload "${BINARY_DIR}/csd_trace_stress.cfg")

file(WRITE "${trace}" "0 M 0 4 4 3\n")
foreach (i RANGE 1 100000)
  math(EXPR tick "${i} * 1000")
  file(APPEND "${trace}" "${tick} C 0 4 4 ${i}\n")
endforeach ()

file(WRITE "${workload}" "
[global]
Mode = 1
LogPeriod = 0
LogFile = STDOUT
DebugLogFile =
LatencyLogFile =
ProgressPeriod = 0
Interface = 1
Scheduler = 0
SubmissionLatency = 5us
CompletionLatency = 5us

[generator]
io_size = 64
readwrite = read
rwmixread = 0.5
blocksize = 512
blockalign = 512
iomode = sync
iodepth = 1
offset = 0
size =
thinktime = 0
randseed = 13245
time_based = 0
runtime = 10s
csd_matrix_slba = 0
csd_rows = 4
csd_cols = 4
csd_matrix_count = 1
csd_vector_seed = 7
csd_opcode = 0xC0
csd_use_sgl = 0
csd_prewrite_matrix = 0
csd_verify_output = 0

[trace]
File = ${trace}
TimingMode = 1
QueueDepth = 32
IOLimit = 0
Regex = \"(\\d+) +(\\w+) +(\\d+) +(\\d+) +(\\d+) +(\\d+)\"
Operation = 2
ByteOffset =
ByteLength =
LBAOffset = 3
LBALength = 4
Second =
Millisecond =
Microsecond =
Nanosecond =
Picosecond = 1
LBASize = 512
UseHexadecimal = 0
MatrixSLBA = 3
Rows = 4
Cols = 5
VectorSeed = 6
MatrixSeed = 6
CSDOpcode = 0xC0
CSDUseSGL = 0
CSDVerifyOutput = 1
")

execute_process(
  COMMAND "${PROGRAM}" "${workload}" "${SSD_CONFIG}" .
  WORKING_DIRECTORY "${ROOT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 600
)

if (NOT "${result}" STREQUAL "0")
  message(STATUS "${output}")
  message(STATUS "${error}")
  message(FATAL_ERROR "CSD trace stress workload failed")
endif ()

set(combined "${output}\n${error}")
if (NOT combined MATCHES "csd\\.pu\\.command\\.count[ \t]+100000\\.")
  message(STATUS "${combined}")
  message(FATAL_ERROR "CSD trace stress did not issue 100000 PU commands")
endif ()

if (NOT combined MATCHES "VerifiedReadCompute: 100000")
  message(STATUS "${combined}")
  message(FATAL_ERROR "CSD trace stress did not verify 100000 outputs")
endif ()

message(STATUS "CSD trace stress workload passed")
