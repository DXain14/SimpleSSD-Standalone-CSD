# SimpleSSD Standalone CSD Extension

This repository is an unofficial research extension of
[SimpleSSD Standalone](https://github.com/simplessd/simplessd-standalone).
It provides a standalone workload and test entry point for the CSD
`read_compute` extension.

The embedded `simplessd` directory is a Git submodule pinned to the public
CSD-modified SimpleSSD repository at commit
`1c886626d5abecdc87d69ba7388db4a26869e6b8`. This repository is an unofficial
GitHub fork published at
`https://github.com/DXain14/SimpleSSD-Standalone-CSD`.

## Quick start

Clone the repository with submodules, then build from its root:

```bash
git clone --branch 2.0 --recurse-submodules \
  https://github.com/DXain14/SimpleSSD-Standalone-CSD.git
cd SimpleSSD-Standalone-CSD
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -L csd -E stress --output-on-failure
```

Run stress tests separately:

```bash
ctest --test-dir build \
  -R '^(csd_trace_stress|csd_stress|csd_gc_cost_benefit_stress)$' \
  --output-on-failure
```

For the detailed Chinese usage guide, see
[`CSD_USAGE_CN.md`](CSD_USAGE_CN.md).

## CSD extension

The current prototype provides:

- Vendor-specific NVMe `read_compute`.
- Dense FP16 GEMV with FP32 accumulation and output.
- Matrix preload through ordinary NVMe WRITE.
- Request-generator and trace-replayer entry points.
- Direct NVMe, negative, timing, source-contract, and stress tests.

This is a simulator extension. It does not implement complete attention
offload, the SparF algorithm, GPU-CSD peer-to-peer DMA, or a complete KV-cache
management system.

## Related work

The research context includes:

> Xiurui Pan, Endian Li, Qiao Li, Shengwen Liang, Yizhou Shan, Ke Zhou,
> Yingwei Luo, Xiaolin Wang, and Jie Zhang. "InstAttention: In-Storage
> Attention Offloading for Cost-Effective Long-Context LLM Inference.”
> 2025 IEEE International Symposium on High Performance Computer Architecture.
> DOI: 10.1109/HPCA61900.2025.00113.

The paper is cited as related work only; this repository is not a
reimplementation of the complete InstAttention system.

## License and provenance

SimpleSSD Standalone and this extension are distributed under GNU GPLv3. See
[`LICENSE`](LICENSE), [`NOTICE.md`](NOTICE.md), and
[`CSD_MODIFICATIONS.md`](CSD_MODIFICATIONS.md).

The original SimpleSSD, SimpleSSD Standalone, DRAMPower, McPAT, and inih
copyright and license notices remain applicable. This repository is not an
official upstream release.
