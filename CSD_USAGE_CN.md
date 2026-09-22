# SimpleSSD Standalone CSD/read_compute 使用说明

本文档说明本仓库中的 CSD（Computational Storage Device）仿真扩展、
配置方式、Standalone workload 入口、测试方法和当前限制。

本项目是基于 SimpleSSD 的研究型仿真器扩展，不代表任何商业 CSD 产品，
也不声称实现完整的 NVMe CSD 标准或特定厂商硬件。

## 1. 仓库结构和来源

本仓库的 `simplessd/` 目录是 Git 子模块，提供 CSD 核心实现。本仓库
提供 Standalone workload 外壳、request generator、trace replayer 和
端到端测试。

全新 clone 后必须递归初始化子模块：

```bash
git clone --branch 2.0 --recurse-submodules \
  https://github.com/DXain14/SimpleSSD-Standalone-CSD.git
cd SimpleSSD-Standalone-CSD
```

standalone 的子模块已经指向公开可访问的 CSD SimpleSSD fork：

```text
https://github.com/DXain14/SimpleSSD-CSD.git
commit 1c886626d5abecdc87d69ba7388db4a26869e6b8
```

## 2. 当前功能范围

当前实现的是 CSD v1 原型：

- SSD Controller 内新增一个 Processing Unit（PU）。
- 新增 NVMe vendor-specific NVM command：`read_compute`。
- 默认 opcode 为 `0xC0`，允许配置为 `0xC0-0xFF`。
- 当前只支持 dense GEMV：

```text
y = A * x
```

- Matrix `A` 使用 row-major FP16 payload。
- Vector `x` 通过 `read_compute` control buffer 从 host DMA 传入，格式为
  FP16。
- PU 内部使用 FP32 累加。
- Output `y` 通过 DMA 写回 host，格式为 FP32。
- Matrix 数据通过 FTL logical-to-physical 映射读取模拟 flash-array
  payload。

本仓库不实现：

- 完整 attention offload 系统。
- InstAttention 的 SparF 算法。
- GPU-CSD peer-to-peer DMA。
- 完整 KV-cache 管理系统。
- 真实硬件固件或商业 NVMe vendor command 兼容性。

## 3. 数据路径

Matrix 预写路径：

```text
普通 NVMe WRITE
  -> host DMA read payload
  -> HIL/ICL/FTL write
  -> FTL 选择物理页
  -> FlashArrayStore 保存真实 bytes
```

`read_compute` 路径：

```text
read_compute
  -> DMA 读取 descriptor 和 vector
  -> FTL 解析 matrix logical range
  -> 从 physical FlashArrayStore 读取 matrix bytes
  -> PU 执行 FP16 -> FP32 GEMV
  -> DMA 写回 FP32 output
  -> NVMe completion
```

因此，`read_compute` 不绕过 FTL，也不是直接从 host-side matrix mock
读取数据。

## 4. 构建

从 standalone 仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build -j 4
```

可选的 sanitizer 构建：

```bash
cmake -S . -B build-asan -DCSD_SANITIZERS=ON
cmake --build build-asan -j 4
```

## 5. CSD SSD 配置

SSD 配置文件的 `[csd]` 段示例：

```ini
[csd]
Enable = 1
ReadComputeOpcode = 0xC0
PUComputeGFLOPS = 13.3
InternalReadBandwidth = 11200000000
MaxMatrixBytes = 268435456
MaxControlBytes = 67108864
RequireICLCacheOff = 1
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| `Enable` | 是否启用 CSD PU 和 `read_compute` |
| `ReadComputeOpcode` | vendor-specific opcode，必须在 `0xC0-0xFF` |
| `PUComputeGFLOPS` | PU GEMV 计算吞吐，单位 GFLOPS |
| `InternalReadBandwidth` | flash array 到 PU 的内部读带宽，单位 bytes/s |
| `MaxMatrixBytes` | 单条命令允许读取的最大 matrix bytes |
| `MaxControlBytes` | 单条命令 control buffer 最大 bytes |
| `RequireICLCacheOff` | 兼容配置项；CSD 开启时要求 ICL cache 关闭 |

CSD 开启时必须关闭 ICL read/write cache：

```ini
[icl]
EnableReadCache = 0
EnableWriteCache = 0
```

原因是 CSD 使用 physical flash-array payload 作为数据源。若写缓存开启，
普通 WRITE completion 后数据可能仍未进入 payload store，无法保证
`read_compute` 读取真实的已写入 matrix。

## 6. Standalone 主程序

主程序参数为：

```text
simplessd-standalone <workload.cfg> <ssd.cfg> <output-directory>
```

第三个参数用于生成日志和输出文件。使用标准输出时可以传当前目录：

```bash
./build/simplessd-standalone \
  test/csd_generator_workload.cfg \
  test/csd_enabled.cfg \
  .
```

## 7. Request generator 模式

最小 generator 配置位于：

```text
test/csd_generator_workload.cfg
```

其中关键配置包括：

```ini
[global]
Mode = 0
Interface = 1

[generator]
readwrite = readcompute
blocksize = 512
blockalign = 512
iomode = sync
iodepth = 1
csd_matrix_slba = 0
csd_rows = 4
csd_cols = 4
csd_matrix_count = 2
csd_vector_seed = 7
csd_opcode = 0xC0
csd_use_sgl = 0
csd_prewrite_matrix = 1
csd_verify_output = 1
```

说明：

- `Mode = 0` 使用 request generator。
- `Interface = 1` 表示 NVMe，CSD workload 必须使用 NVMe。
- `readwrite = readcompute` 或 `randreadcompute` 生成 CSD GEMV 请求。
- `csd_prewrite_matrix = 1` 先通过普通 NVMe WRITE 预写真实 matrix。
- `csd_verify_output = 1` 使用 CPU reference GEMV 校验 FP32 输出。

运行：

```bash
./build/simplessd-standalone \
  test/csd_generator_workload.cfg \
  test/csd_enabled.cfg \
  .
```

## 8. Trace replayer 模式

最小 trace workload 配置位于：

```text
test/csd_trace_workload.cfg
```

trace 文件：

```text
test/csd_trace_readcompute.txt
```

示例内容：

```text
0 M 0 4 4 3
1000 C 0 4 4 7
```

操作含义：

- `M`：生成 matrix payload，并通过普通 NVMe WRITE 写入 flash array。
- `C`：读取前序 `M` 写入的 matrix，执行 `read_compute`。

运行：

```bash
./build/simplessd-standalone \
  test/csd_trace_workload.cfg \
  test/csd_enabled.cfg \
  .
```

当 `CSDVerifyOutput = 1` 时，程序会检查：

- `C` 是否存在同一 matrix SLBA 的前序 `M`。
- matrix rows/cols 是否匹配。
- NVMe completion status 是否为 0。
- FP32 output 是否与 CPU reference GEMV 一致。

## 9. read_compute control buffer

当前 control buffer 布局如下：

| Offset | 类型 | 含义 |
| --- | --- | --- |
| `0` | char[4] | magic，必须为 `CSD0` |
| `4` | u16 | version，当前为 `1` |
| `6` | u16 | op，当前 `1` 表示 GEMV |
| `8` | u64 | flags，当前为 `0` |
| `16` | u64 | `matrix_slba` |
| `24` | u32 | `rows` |
| `28` | u32 | `cols` |
| `32` | u32 | `lda`，v1 要求等于 `cols` |
| `40` | u64 | `vector_offset` |
| `48` | u64 | `output_offset` |

约束：

- descriptor 固定 56 bytes。
- vector 为 `cols * sizeof(uint16_t)` bytes。
- output 为 `rows * sizeof(float)` bytes。
- 输入和输出区域不能覆盖 descriptor。
- control buffer 不能超过 `MaxControlBytes`。
- matrix 不能超过 `MaxMatrixBytes`。
- matrix logical range 必须有效且已映射。

## 10. 测试

运行全部非 stress CSD 测试：

```bash
ctest --test-dir build -L csd -E stress --output-on-failure
```

运行 source contract：

```bash
ctest --test-dir build -R csd_source_contract --output-on-failure
```

运行 stress 测试：

```bash
ctest --test-dir build \
  -R '^(csd_trace_stress|csd_stress|csd_gc_cost_benefit_stress)$' \
  --output-on-failure
```

当前测试覆盖：

- FP16 解码和 GEMV 单元测试。
- direct NVMe smoke/integration。
- generator 和 trace 入口。
- CSD disabled 普通 I/O。
- 自定义 opcode 和 `0xFF` opcode。
- 非法 opcode、非法尺寸和非法配置。
- cache 冲突拒绝。
- 内部 bandwidth/compute timing scaling。
- workload completion status 传播。
- matrix payload、GC 和压力测试。
- standalone 内部 SimpleSSD source contract。

## 11. 普通 I/O 兼容性

当 `CSD Enable = 0` 时，普通 NVMe WRITE 保持原 SimpleSSD 的并发时序：
host DMA read 和 SSD write 可以并发，completion 等待两个事件完成。

当 `CSD Enable = 1` 时，普通 WRITE 为了保存真实 payload，host DMA 必须
先完成，FTL 才能将 payload 写入 physical flash-array store。

该差异是 CSD payload-accurate 模式的数据依赖，不应解释为普通模式的
通用性能优化。

## 12. 相关工作与许可证

本项目的研究背景包括：

> Xiurui Pan, Endian Li, Qiao Li, Shengwen Liang, Yizhou Shan, Ke Zhou,
> Yingwei Luo, Xiaolin Wang, and Jie Zhang. "InstAttention: In-Storage
> Attention Offloading for Cost-Effective Long-Context LLM Inference.”
> 2025 IEEE International Symposium on High Performance Computer Architecture.
> DOI: 10.1109/HPCA61900.2025.00113.

该论文只作为相关工作引用。本仓库不包含论文 PDF，也不声称复现其完整
attention offload 系统。

本项目基于 SimpleSSD 和 SimpleSSD Standalone，使用 GNU GPLv3。发布或
再分发时必须保留：

- `LICENSE`
- `NOTICE.md`
- `CSD_MODIFICATIONS.md`
- embedded SimpleSSD 的许可证和版权声明
- DRAMPower、McPAT、inih 的第三方许可证和版权声明

详细来源信息见 `UPSTREAM.md`。
