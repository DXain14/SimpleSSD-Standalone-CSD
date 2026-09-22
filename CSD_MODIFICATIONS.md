# CSD Modification Record

This record identifies the modification boundary relative to upstream
SimpleSSD Standalone and its embedded SimpleSSD source.

## Bases

- Standalone upstream branch: `2.0`
- Standalone base commit: `8255367ff8736f3dce16612054114a3ffed5e7ab`
- Embedded SimpleSSD local base: `73ad8ad5220ce5ebfeb055687d0328765a046c0f`
- Working release date: 2026-09-21

## Functional changes

- Added `BIO_READ_COMPUTE` and status-aware completion propagation.
- Added request-generator support for `readcompute` and
  `randreadcompute`.
- Added trace `M` matrix preload and `C` read_compute operations.
- Added deterministic FP16 payload generation and FP32 reference GEMV checks.
- Added direct NVMe, negative, timing, source-contract, and stress tests.
- Added sanitizer-oriented completion cleanup and CSD-disabled timing
  regression coverage.

The exact file-level change set is recorded by Git.

- Public CSD copyright identifier: `DXain14`.
- Before public release, the release owner should retain an internal record
  mapping this public identifier to the legally authorized human or
  organization owner.
