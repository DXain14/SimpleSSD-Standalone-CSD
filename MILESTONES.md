# CSD Milestones

## CSD-001: NVMe/FTL/PU Functional Path

- Added a CSD processing unit that accepts the vendor-specific NVMe
  read_compute command.
- Added a physical flash-array payload store and made CSD reads use the FTL
  logical-to-physical mapping before reading array payload bytes.
- Added dense FP16 GEMV with FP32 accumulation and FP32 output writeback.
- Added direct NVMe CSD smoke, integration, timing, disabled-mode, and stress
  tests.

## CSD-002: Unified Standalone Workload Entry

- Added `BIO_READ_COMPUTE` so read_compute can travel through the same
  BlockIOEntry and scheduler path as ordinary read, write, flush, and trim.
- Added optional BIO write payloads so request-generator matrix prewrites store
  real FP16 matrix bytes through ordinary NVMe WRITE commands.
- Extended the NVMe standalone driver to encode `BIO_READ_COMPUTE` into the
  CSD vendor command from `submitIO(BIO&)`.
- Extended request-generator mode with `readcompute` and `randreadcompute`.
- Extended trace-replayer mode with `C` operations carrying matrix SLBA, rows,
  columns, and vector seed fields.
- Added end-to-end workload tests that invoke the original
  `simplessd-standalone workload.cfg ssd.cfg .` entry point.

## CSD-003: Workload Robustness and Verification

- Propagated NVMe completion status through BIO callbacks so generator and
  trace workloads fail on non-zero completion status instead of treating it as
  success.
- Added shared CSD workload helpers for checked dimensions, transfer sizing,
  deterministic FP16 payload generation, FP32 reference GEMV, and output
  comparison.
- Added trace `M` matrix preload operations so trace-mode GEMV reads real
  matrix payloads written through ordinary NVMe WRITE into the flash array.
- Added generator and trace output verification counters:
  `VerifiedReadCompute` and `FailedReadCompute`.
- Added trace NVMe-interface precheck, dimension/overflow negative tests,
  opcode status-propagation tests, and QD32/100000 trace stress verification.

## CSD-004: Sanitizer-Clean Completion Shutdown

- Made NVMe Controller completion contexts owned by the Controller while CQ DMA
  writeback and CPU completion callbacks are pending.
- Added Controller destructor cleanup for pending completion contexts so
  Standalone workloads that stop the event engine do not leak completion
  writeback state.
- Kept normal completion behavior intact and routed interrupt coalescing-only
  completions through the same posting path.
- Verified the QD32/100000 trace stress test and the non-stress CSD suite under
  ASan/UBSan/LSan.

## CSD-005: Ordinary WRITE Timing Compatibility

- Restored the original SimpleSSD ordinary NVMe WRITE overlap when CSD is
  disabled: host DMA read and SSD write run concurrently, and completion waits
  for both.
- Kept CSD-enabled WRITE payload-accurate: host DMA must complete before FTL can
  store physical flash-array payload bytes.
- Replaced namespace `beginAt` completion-count reuse with an explicit
  `completedEvents` counter for WRITE/READ/COMPARE two-event completions.
- Added a direct NVMe timing regression test that proves CSD-disabled writes are
  not serialized by the CSD payload path.
- Added `namespace.hh` and `pal/pal_old.*` to the CSD source contract so the
  embedded SimpleSSD tree cannot silently drift from the CSD implementation.
