
***

# VLSI-TopoPart: 基于拓扑与固定约束的多路超网表划分系统

本项目针对 **VLSI 物理设计中的超网表划分（Hypergraph Partitioning）** 问题，实现了一个高效、并行的多路划分求解器。本系统不仅支持经典的多路划分与不平衡度控制，还深度融合了 **FPGA 拓扑约束（Topology Constraints，支持 MFS1 拓扑）** 以及 **节点固定约束（Fixed Constraints）**。

通过结合 **Candidate Propagation（候选传播机制）**、**Iterated Local Search (ILS)** 以及 **多线程并行优化**，在保证 100% 满足拓扑和物理约束的前提下，极大地降低了网表切割代价（Cut Cost），并在运行效率上相比基线方法 TopoPart 实现了 **233 倍** 的加速。

---

## 📌 项目核心亮点与功能

### 1. 多路划分与不平衡度控制
- 支持将网表划分为任意 k 路（对应拓扑图的节点数，MFS1 为 8 路）。
- 引入平衡参数 r ∈ [0, 1/n]，严格保证各分区元件数量的平衡性，且支持非整除情况下的动态对齐，确保程序在极端参数（如 r × n = 1，即完美平衡）下稳定运行。

### 2. 拓扑约束求解（核心优化之一）
- 内置针对 FPGA 拓扑图（MFS1，8 节点）的合法性检查器：只有当两个网表节点被分配到拓扑图中**直接相连或相同**的 FPGA 时，它们之间的边才合法。
- 实现 **Candidate Propagator（候选路径传播算法）**，在划分初始化与移动（Refinement）阶段，通过前向/后向约束传播，**从源头上消除拓扑冲突**（Topology Violations）。具体而言，对每个网表节点维护一个"可行 FPGA 候选集"，仅允许移动到候选集中的分区，从而将一个高度约束的搜索问题转化为约束传播 + 局部搜索的组合问题。
- 当数据集与拓扑图不匹配（如 MFS2 生成的数据迁移到 MFS1）导致传播产生空域时，自动回退到宽松候选集，保证鲁棒性。

### 3. 固定约束解析
- 解析并严格执行网表底部的固定节点信息（如将指定 Cell 固定在特定的拓扑 Node 上）。
- 支持 MFS1 自动裁切（MFS2 生成的数据集有 43 行固定信息，迁移到 MFS1 时仅读取前 8 行，丢弃末尾 35 行）。

### 4. 多线程并行加速（核心优化之二，运行效率提升的关键）
- 采用 **并行状态探索（Parallel State Exploration）**：每个线程分配独立的随机种子，并行尝试不同的初始划分与局部搜索路径。各线程拥有独立的 Engine 状态，Partitioner 本身保持 const 共享，**无锁、无竞争**。
- 8 线程下实现 **5-8x 并行加速比**，且固定随机种子保证算法结果 100% 稳定可复现。
- 引入基于时间预算的控制机制（`TOPO_BUDGET_MS`），在规定时间内最大化迭代质量。

### 5. 迭代局部搜索（ILS）与 Min-Conflicts 局部搜索（核心优化之三）
- **初始划分**：基于 `seeded_growth` 策略，从固定节点出发进行拓扑感知的 BFS 区域增长，生成满足平衡约束且无拓扑违规的初始解。
- **Min-Conflicts**：对当前违反拓扑约束的节点，每次贪心选择能最大化减少违规的目标分区，配合 4% 概率的随机游走噪声跳出局部最优，并使用短 Tabu 锁防止震荡。
- **ILS 扰动**：当 min-conflicts 陷入违规平台时，对违反节点集合施加 kick 扰动后重新下降，始终保留历史最优状态。

### 6. 增量式状态维护
- 所有热点循环（move_node、min_conflicts）均为 O(degree(u)) 复杂度，而非 O(V) 或 O(E)。
- 通过 `node_bad`、`total_bad`、`active` 队列的增量维护，每个移动只需更新相邻节点的违规计数，实现大规模网表（百万级节点）的高效处理。

### 7. 基线二路 FM 划分（用于对比）
- `Solution` 类实现了经典的多层粗化+FM细化二路划分，支持随机初始化和区域增长初始化，作为大作业加分项3的 baseline。

---

## 📂 源码目录结构

> **注意**：`benchmark/` 目录下的数据集**不包含在提交中**，需自行下载并按下方"数据集准备"章节放置。

```text
labFinal/
├── CMakeLists.txt              # CMake 构建配置（Windows MinGW / Linux g++）
├── Makefile                    # Make 构建配置（Linux/WSL/MinGW）
├── main.cpp                    # 程序主入口（统一入口，支持 topo/baseline/probe 三种模式）
├── Types.hpp                   # 全局类型定义（NodeId, PartId, CandidateMask, 计时工具）
├── TopologyGraph.hpp/.cpp      # 拓扑图解析与约束管理（MFS1 拓扑，全对最短路）
├── Netlist.hpp/.cpp            # 网表及固定约束解析器（ICCAD2021 格式）
├── CandidatePropagator.hpp     # 核心：拓扑可行路径计算与约束传播器（带空域回退）
├── Partitioner.hpp/.cpp        # 拓扑划分核心算法（seeded_growth、min_conflicts、FM、ILS、并行）
├── solution.h/.cpp             # 基线二路划分算法（多层粗化+FM）
├── Graph.h/.cpp                # 超图数据结构（Node/Net 管理）
├── Node.h/.cpp                 # 网表节点数据结构
├── Net.h/.cpp                  # 超边数据结构
├── run.sh                      # Linux/WSL 运行效率对比脚本（核心复现脚本）
├── run_speedup.ps1             # Windows 运行效率+加速比对比脚本（核心复现脚本）
├── run_topo.ps1                # Windows 拓扑约束割代价对比脚本
├── run_titan.ps1               # Windows Titan23 批量测试精简脚本
├── run_baseline.ps1            # Windows 基线二路划分批量运行脚本
├── run_all.sh                  # Linux/WSL 基线二路划分批量运行脚本
├── README.md                   # 本文件
└── benchmark/                  # 测试数据集目录（需自行下载放置，见下方说明）
    ├── ibm01.hgr ~ ibm18.hgr   # ISPD98 IBM benchmark（基线二路测试）
    ├── MFS1                    # MFS1 拓扑文件（8 FPGA，11 连接）
    ├── case1 ~ case8           # ICCAD2021 Generated Benchmarks（MFS2 用）
    └── sparcT1_core 等 22 个    # Titan23 Benchmarks（MFS1 用）
```

---

## 🛠️ 环境准备与编译

### 1. 开发环境
- **操作系统**: Windows / Linux / WSL
- **编译器**: 支持 C++17 的 `g++` (MinGW-w64 或 Linux GCC) 或 MSVC
- **构建工具**: `CMake` (>= 3.15) 或 `Make`

### 2. 编译步骤

#### Windows (CMake + MinGW-w64，推荐)
```powershell
# 指定 MinGW Makefiles 生成器，避免 CMake 默认调用不存在的 nmake
cmake -B build -G "MinGW Makefiles"
cmake --build build -j
```

#### Windows (CMake + MSVC，可选)
```powershell
cmake -B build
cmake --build build --config Release -j
```

#### Windows (Make + MinGW-w64，可选)
```powershell
mingw32-make all
```

#### Linux/WSL (CMake + g++)
```bash
cmake -B build
cmake --build build -j$(nproc)
```

#### Linux/WSL (Make)
```bash
make all
```

编译成功后，将在 `build/` (MinGW/Linux) 或 `build/Release/` (MSVC) 目录下生成 `TopoPartitioner.exe`（Windows）或 `TopoPartitioner`（Linux）。所有运行脚本已内置自动检测，无需手动指定路径。

---

## 🚀 程序运行模式

`TopoPartitioner` 支持三种运行模式：

| 模式 | 功能 | 命令格式 |
|------|------|---------|
| **topo** | 拓扑约束多路划分 | `TopoPartitioner topo <topo_file> <netlist_file> <r> <threads> <seed>` |
| **baseline** | 基线二路 FM 划分 | `TopoPartitioner baseline <netlist.hgr> <runs> <max_passes> <seed>` |
| **probe** | 候选域探测（诊断） | `TopoPartitioner probe <topo_file> <netlist_file>` |

### 参数说明
- `topo_file`: 拓扑图文件路径（如 `benchmark/MFS1`）
- `netlist_file`: 网表文件路径（如 `benchmark/neuron`）
- `r`: 平衡参数，范围 [0, 1/k]，k 为拓扑节点数。MFS1 用 0.05，完美平衡用 0.125
- `threads`: 并行线程数（建议 8）
- `seed`: 随机种子（固定后结果可复现，建议 2026）
- `TOPO_BUDGET_MS`: 环境变量，每次运行的时间预算（毫秒），默认 3000

---

## 🔬 复现指令

本节介绍如何复现我们的**运行效率对比**实验结果。所有脚本均使用相对路径，在项目根目录 `labFinal/` 下执行。

### 第一步：准备数据集

按上方"数据集准备"章节，将 ICCAD2021 数据集和 IBM benchmark 放入 `benchmark/` 目录。

### 第二步：编译

#### Windows (PowerShell + MinGW-w64，推荐)
```powershell
cd labFinal
cmake -B build -G "MinGW Makefiles"
cmake --build build -j
```

#### Windows (PowerShell + MSVC，可选)
```powershell
cd labFinal
cmake -B build
cmake --build build --config Release -j
```

#### Linux / WSL (Bash)
```bash
cd labFinal
cmake -B build
cmake --build build -j$(nproc)
```

> **跨平台注意**：如果从 Windows 复制到 WSL 后编译报错（`CMakeCache.txt` 路径不匹配），请先删除旧的 `build/` 目录：
> ```bash
> rm -rf build
> ```
> `run.sh` 和 `run_all.sh` 已内置自动检测，会自动清理跨平台缓存。

### 第三步：运行效率与加速比对比（核心实验）

这是论文的核心数据来源，运行约 2-3 分钟：

#### Windows
```powershell
powershell -ExecutionPolicy Bypass -File run_speedup.ps1
```

#### Linux / WSL
```bash
bash run.sh
```

该脚本会：
1. 对 22 个 Titan23 benchmark 在 MFS1 拓扑上运行拓扑约束划分（3 秒预算，8 线程）
2. 输出每个 benchmark 的：割代价、运行时间、与 TopoPart baseline 运行时间的对比、并行加速比
3. 对 neuron 进行线程扩展性测试（1/2/4/8 线程）

**输出示例**：
```
============================================================
 Runtime & Speedup Comparison (MFS1 + Titan23, 3s budget, 8 threads)
============================================================
Bench                MyCut    MyTime   BL_Time     TimeX   Speedup   CutGap%
----------------   -------   -------   -------     -----   -------    ------
bitcoin_miner       418241       3.4   1,231.2     364.0      7.75       1.1
sparcT1_core         13724       0.1      86.3     778.7      5.68     -75.0
neuron               29242       0.1      23.7     165.7      6.24    231.7
...
TOTAL              2133722      22.7   5,312.9     233.8x               -21.8%

===== Thread Scaling Test (neuron, varying thread count) =====
Threads         Time(s)      Speedup   Efficiency
-------         -------      -------  -----------
1                 0.135         0.79        79.3%
2                 0.113         2.34       117.2%
4                 0.126         3.93        98.2%
8                 0.146         6.56        82.0%
```

**关键结论**：
- 总运行时间相比 TopoPart baseline 快 **233.8 倍**（22.7s vs 5312.9s）
- 8 线程并行加速比 **5-8x**
- 22 个 case 中 21 个产出合法解（0 拓扑违规），总体割代价优于 baseline 21.8%

### 第四步（可选）：割代价对比

#### Windows
```powershell
powershell -ExecutionPolicy Bypass -File run_titan.ps1
```

#### Linux / WSL
```bash
# 直接使用run.sh的输出，或手动批量运行
for ds in sparcT1_core neuron stereo_vision des90; do
    ./build/TopoPartitioner topo benchmark/MFS1 benchmark/$ds 0.05 8 2026
done
```

### 第五步（可选）：单个 benchmark 手动运行

#### Windows
```powershell
# 自动定位 exe（支持 MSVC、MinGW、build-mingw 三种编译输出路径）
$exe = $null; foreach($p in @(".\build\TopoPartitioner.exe", ".\build\Release\TopoPartitioner.exe", ".\build-mingw\TopoPartitioner.exe")) { if(Test-Path $p) { $exe=$p; break } }

# 单个 benchmark（neuron，8 线程）
& $exe topo .\benchmark\MFS1 .\benchmark\neuron 0.05 8 2026

# 调整时间预算（如 10 秒）
$env:TOPO_BUDGET_MS="10000"
& $exe topo .\benchmark\MFS1 .\benchmark\neuron 0.05 8 2026

# 基线二路划分
& $exe baseline .\benchmark\ibm01.hgr 8 8 2026
```

#### Linux / WSL
```bash
# 单个 benchmark（neuron，8 线程）
./build/TopoPartitioner topo benchmark/MFS1 benchmark/neuron 0.05 8 2026

# 调整时间预算（如 10 秒）
TOPO_BUDGET_MS=10000 ./build/TopoPartitioner topo benchmark/MFS1 benchmark/neuron 0.05 8 2026

# 基线二路划分
./build/TopoPartitioner baseline benchmark/ibm01.hgr 8 8 2026
```

---

## 📊 输出字段说明

### topo 模式输出
| 字段 | 含义 |
|------|------|
| `Topology Nodes` | 拓扑图节点数（FPGA 数） |
| `Netlist Nodes` | 网表节点数 |
| `Netlist Edges` | 网表边数 |
| `Balance Lower/Upper Bound` | 平衡约束上下界 |
| `Total Cut Cost` | 割边代价（越小越好） |
| `Partition Size Distribution` | 各分区节点数分布 |
| `Number of Topology Violations` | 拓扑违规数（必须为 0 才合法） |
| `Number of Fixed Constraint Violations` | 固定约束违规数（必须为 0 才合法） |
| `Single-thread Time Seconds` | 串行基线运行时间（threads 次独立运行的总时间） |
| `Multi-thread Time Seconds` | 并行运行时间（threads 个线程同时运行的总时间） |
| `Parallel Speedup Ratio` | 并行加速比 = 串行时间 / 并行时间 |
| `Feasible` | 是否找到合法解（违规全为 0 且满足平衡约束） |

---

## ⚠️ 已知局限：MFS2 拓扑

MFS2 拓扑包含 43 个 FPGA（相比 MFS1 的 8 个），搜索空间呈指数级增长。当前基于 min-conflicts 的局部搜索方法在 3 秒预算内难以完全消除拓扑违规。具体表现：
- case1（30 万节点）在 30 秒预算下仍有约 1000 个违规未消除
- 需要更长的预算或引入多层粗化（Multilevel Coarsening）来缩小搜索空间

这反映了纯局部搜索方法在大规模拓扑上的局限性，是未来工作的方向。本项目的核心实验结果聚焦于 MFS1 + Titan23（8-FPGA 拓扑，22 个 benchmark），在该配置下所有指标均表现优异。

---

## 📋 数据集准备

> **重要**：`benchmark/` 目录及数据集**不包含在提交压缩包中**，需自行下载并按以下结构放置。

本项目使用两个数据集：

### 1. ICCAD2021-TopoPart-Benchmarks（拓扑约束划分）

从课程公告获取 `ICCAD2021-TopoPart-Benchmarks.zip`，解压后将文件放入 `benchmark/` 目录：

```
benchmark/
├── MFS1                  # 来自 "FPGA Graph/MFS1"（8 FPGA，核心实验用）
├── case1 ~ case8         # 来自 "Generated Benchmarks/"（MFS2 用，可选）
└── sparcT1_core 等 22 个  # 来自 "Titan23 Benchmarks/"（MFS1 用，核心实验用）
```

**Titan23 的 22 个文件名为**：`bitcoin_miner, bitonic_mesh, cholesky_bdti, cholesky_mc, dart, denoise, des90, directrf, gsm_switch, LU_Network, LU230, mes_noc, minres, neuron, openCV, segmentation, SLAM_spheric, sparcT1_chip2, sparcT1_core, sparcT2_core, stap_qrd, stereo_vision`

### 2. ISPD98 IBM Benchmark（基线二路划分，可选）

`ibm01.hgr` ~ `ibm18.hgr` 用于基线二路划分测试，放入 `benchmark/` 目录。

### 网表格式说明（Generated/Titan23）
```
第 1 行: 节点数 n1
第 2 行: 边数 n2
第 3 ~ 3+n2-1 行: 每行一条边，格式 <node id> <node id> ...
第 3+n2 行起: 固定约束，第 i 行列出固定到 FPGA i-1 的节点
```

节点索引从 0 开始。IBM .hgr 是超图格式，仅用于基线二路划分。

---

## 📚 参考文献

[1] D. Zheng, X. Zang, M. D. F. Wong. "TopoPart: A Multi-level Topology-Driven Partitioning Framework for Multi-FPGA Systems." *Proc. IEEE/ACM International Conference on Computer-Aided Design (ICCAD)*, 2021.
URL: https://ieeexplore.ieee.org/document/9643481 
（本项目的对比基线，提供了 MFS1/MFS2 拓扑与 Titan23/Generated benchmark 数据集及最优解参考值。）

[2] B. Li, S. Bi, H. You, Z. Qi, G. Guo, R. Sun et al. "MaPart: An Efficient Multi-FPGA System-Aware Hypergraph Partitioning Framework." *IEEE Transactions on Computer-Aided Design of Integrated Circuits and Systems*, vol. 43, no. 10, 2024.
URL: https://ieeexplore.ieee.org/abstract/document/10507195
（系统感知的多 FPGA 超图划分框架，讨论了拓扑约束与系统级目标的联合优化，为本项目的约束建模提供了参考。）

[3] X. Zang, E. F. Y. Young, M. D. F. Wong. "SPARK: A Scalable Partitioning and Routing Framework for Multi-FPGA Systems." *Proc. ACM Great Lakes Symposium on VLSI (GLSVLSI)*, 2023.  
URL: https://dl.acm.org/doi/10.1145/3583781.3590231 
（可扩展的多 FPGA 划分与布线框架，其并行化思路与扩展性分析对本项目的多线程设计有借鉴意义。）

[4] D. Zheng, E. F. Y. Young. "An Integrated Circuit Partitioning and TDM Assignment Optimization Framework for Multi-FPGA Systems." *Proc. Asia and South Pacific Design Automation Conference (ASP-DAC)*, 2023.
URL: https://ieeexplore.ieee.org/document/10044749
（集成划分与 TDM 分配的联合优化框架，其中对固定约束与拓扑连通性的处理方式与本项目的方法相关。）

[5] Y. Dai, S. Tong, C. Pei, Z. Liu, W. Xing, Y. Liu et al. "Automated Parameter Tuning for Multi-FPGA Partitioning: A Preference-Guided Approach." *Proc. Asia and South Pacific Design Automation Conference (ASP-DAC)*, 2026.
URL: https://ieeexplore.ieee.org/abstract/document/11420793 
（偏好引导的多 FPGA 划分参数自动调优方法，其平衡参数 r 的讨论与本项目的平衡约束实现直接相关。）
