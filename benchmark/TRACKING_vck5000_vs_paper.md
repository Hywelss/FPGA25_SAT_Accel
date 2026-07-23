# VCK5000 移植 vs. SAT-Accel 论文 — 对齐追踪

> 论文：Lo, Chang, Cong. *SAT-Accel: A Modern SAT Solver on a FPGA.* FPGA '25, pp. 234–246.
> 代码：`FORMiND-Lab/FPGA25_SAT_Accel`，`main` = 论文 artifact，`vck5000-adaptation` = 本次移植。
> 文档状态：**待测**（board 侧数据尚未采集）
> 最后更新：2026-07-23

---

## 0. 一句话结论

**目前无法给出"差距多少"的数字**——`vck5000-adaptation` 分支上跑过的 8 个测例，
与论文 Table 3/Table 4 的 59 个测例**没有任何一个重合**，两组数据不可比。
本文档给出把两者对齐所需的全部步骤与工具。

---

## 1. 平台对照

| 项 | 论文 (main) | 本次移植 (vck5000-adaptation) | 影响 |
|---|---|---|---|
| 板卡 | Alveo U55C (VU47P, 16 nm) | VCK5000 (VC1902 Versal) | — |
| Platform | `xilinx_u55c_gen3x16_xdma_3_202210_1` | `xilinx_vck5000_gen4x8_qdma_2_202220_1` | — |
| 数据时钟 | **230 MHz** | **223 MHz**（目标 220，Vitis 实测 223） | ≈ **3% 慢**，可直接折算 |
| Vitis / XRT | 2022.2 / 2.14.384 | 2022.2 / 2.14.354 | 无 |
| URAM 总量 | 960（论文用 778 = **81%**） | **463** | 装不下原设计 → 见下 |
| 片外存储 | HBM，9 个 buffer 分散在 6 个 channel | 单个 `MC_NOC0`（DDR over NoC） | bank 并行度丢失，**影响未量化** |
| `_FPGA_MAX_LITERAL_ELEMENTS` | 1,048,576 | **524,288（减半）** | literal/clause store 及 page 池同时减半 |
| `_FPGA_MAX_LEARN_ELE` | 1,024 | 1,024（未改） | 两边一致，非移植引入 |
| 求解器算法代码 | — | **一行未改** | 行为差异只可能来自容量与访存 |

**因此预期差距有三个来源，需要分别归因：**

1. **时钟** 230 → 223 MHz：确定性的 ~3%，对所有测例一致。
2. **访存拓扑** HBM 多 bank → NoC 单端口：只影响访存密集阶段。论文 Table 5 显示
   Propagation 占 29–45% 的时间、是首要瓶颈，所以这一项**可能是主要差距来源**。
3. **容量减半**：不影响能解出来的题的速度，只决定**哪些题解不出来**（覆盖率）。

---

## 2. 当前已有数据（不可与论文对比）

来自 `benchmark/results/summary.md`，2026-07-15 采集，8 个现代 SAT Competition 测例：

| 测例 | 结果 | 说明 |
|---|---|---|
| `unif-r3-v500-c1500-01` | ✅ SAT，kernel 中位数 **0.777 ms**（5/5 一致） | 唯一跑通的 |
| `rphp4_065_shuffled` | ❌ `-4` 学习子句 page 耗尽 | default 与 aggressive 配置均失败 |
| `sp4-33-bin-nons-flat-noid` | ❌ `-4` | 同上 |
| `randomG-B-Mix-n15-d05` | ❌ `-4` | 同上 |
| `hidden-k3-s0-r4-n500-03` | ❌ `-4` | |
| `quad_res_r29_m32` | ❌ `-2` 学习子句 > 1024 文字 | 撞的是 U55C 也有的限制 |
| `sp5-26-19-bin-nons-tree-noid` | ❌ `-2` | 同上 |

**这 7 条的 kernel 时间是"撞限制的时间"，不是求解时间，任何情况下都不能用于加速比。**

⚠️ 这 8 个测例全部不在论文的评测集中，也不在仓库 `SAT_test_cases/` 里
（它们是移植时新加进 `benchmark/` 的）。**这是当前无法对比的唯一原因。**

---

## 3. 论文评测集在仓库中的可用性

论文 Table 4（45 例）+ Table 3（13 例，与 SAT-Hard 对比）= **59 条基线**，
已全部核对存在于 `SAT_test_cases/`（`sat/`、`unsat/`、`satlib/` 三个子目录），
并且 `testcases.sh` 已经把它们全部列好了。

### 静态容量审计：59 条全部装得下

用 `benchmark/capacity_audit.py` 复刻 `host.cpp` 的分配算术（clause store 4 字页 + next 指针、
transposed literal store 16 字页），在 VCK5000 的 524,288 上限下核算：

```
limit = 524288 elements per store
fits      : 59/59
rejected  : 0
```

即**没有任何一条会在 kernel 启动前被 host 拒绝**。clause store 余量最紧的 5 条：

| 测例 | clause store | literal store | 剩余余量 |
|---|---:|---:|---:|
| `16_8_7` | 93.1% | 76.8% | 36,072 |
| `16_8_6` | 79.0% | 65.4% | 110,084 |
| `16_16_3` | 75.8% | 68.2% | 126,636 |
| `logistics-rotate-07t5` | 67.7% | 51.8% | 169,356 |
| `16_8_5` | 64.9% | 54.0% | 183,972 |

⚠️ **静态装得下 ≠ 解得出来**。剩余余量是留给学习子句的动态 page 池，搜索过程中仍可能耗尽（`-4`）。
上面这 5 条余量最少，是 `-4` 的高风险项，Step 2 应优先拿它们探路。

论文数值已转录到 `benchmark/paper_baseline.csv`，字段：

```
instance, repo_path, expect, paper_lits, paper_cls,
paper_sa_ms, paper_minisat_ms, paper_kissat_ms, paper_table
```

字段含义与注意事项（**故意不写在 CSV 里**——注释行会被 Excel/编辑器重新加引号，
把解析器搞崩；这个文件保持纯 RFC-4180 CSV，只有表头 + 59 行数据）：

- `paper_table`：`T4` = 论文 Table 4 主评测集（45 条，有 SA/MiniSat/Kissat 三列）；
  `T3` = 与 SAT-Hard 对比集（13 条，论文只给了 SA 时间，MiniSat/Kissat 字段留空）。
- `paper_sa_ms` / `paper_minisat_ms` / `paper_kissat_ms`：单位 ms，U55C @ 230 MHz。
- `paper_kissat_ms` 为 `<10`：低于 Kissat 的 10 ms 计时分辨率。
  **论文自己算平均加速比时排除了这些行**，我们复算必须沿用同样的排除规则，否则数字对不上。
- `paper_sa_ms` 为 `NA`：论文标 N/A（`hole9_unsat`，未完成）。
- `expect`：1 = SAT，0 = UNSAT，传给 `test.real.out` 的最后一个参数做答案校验。

---

## 4. 实施步骤

### Step 0 — 前置检查（板端，~5 min）

```bash
xbutil examine | grep -i vck5000          # 确认卡在位、拿到 device name
ls -l src/bin/workload-hw.xclbin          # 确认是 VCK5000 build 而不是仓库自带的 U55C 版
xclbinutil --info -i src/bin/workload-hw.xclbin | grep -i "clock\|platform"
```

记录下实际数据时钟（写进 §1 表格），后面折算 3% 时钟差要用。

若 xclbin 还是 U55C 的，先重建：

```bash
XRT_ROOT=/opt/xilinx/xrt VITIS_ROOT=/tools/Xilinx/Vitis/2022.2 ./runCompile.sh hls && ./runCompile.sh hw
```

### Step 1 — 编译 host（板端，~2 min）

必须带 `-DFPGA_VCK5000`，否则 host 侧 `_HOST_MAX_*` 与 kernel 里的容量不一致，
buffer 大小对不上。`runCompile.sh` 在 `PLATFORM` 含 `vck5000` 时会自动注入。

```bash
./runCompile.sh opencl
```

### Step 1.5 — 静态容量审计（任意机器，无需板卡，~10 s）

```bash
python3 benchmark/capacity_audit.py                  # VCK5000 上限 524,288
LIMIT=1048576 python3 benchmark/capacity_audit.py    # U55C 上限，做对照
```

已跑过，结论见 §3：59/59 全部装得下。改动容量常量后重跑此脚本即可立刻看到覆盖率变化，
不用占板子。

### Step 2 — 跑论文测例集（板端，~1–3 h）

```bash
chmod +x benchmark/run_paper_suite.sh
REPEATS=5 ./benchmark/run_paper_suite.sh
```

脚本行为：
- 逐条读 `paper_baseline.csv`，用 `expect` 字段做 SAT/UNSAT 校验
- 每条重复 5 次（论文没说重复次数，5 次足以看出方差；已有数据显示同一题路径完全确定，
  方差只来自 PCIe/调度抖动）
- 退出码 3（片上内存不足）**立即停止重复**并记录，不浪费时间
- 输出：`benchmark/results/paper_suite/{<instance>.csv, <instance>.log, run_status.csv}`

建议先小范围试跑确认链路通：

```bash
ONLY='ssa|nqueens|sudoku' REPEATS=2 ./benchmark/run_paper_suite.sh
```

### Step 3 — 生成对比表（任意机器，~1 s）

```bash
python3 benchmark/compare_to_paper.py
```

产出 `benchmark/results/paper_suite/comparison.{csv,md}`，把 `comparison.md`
的两张表直接贴回本文档 §5 / §6。

**取数说明**：host 写出的 metrics CSV 第 5 列（index 4）是 kernel 执行时间（秒），
来源是 `host.cpp:780` 的 `executionTime/1e9`（OpenCL profiling counter）。
这是与论文 "Time in ms" 口径一致的列。
⚠️ 仓库 README 里说的"ColumnJ 是 FPGA runtime"是错的，J 列是 cycle counter 百分比。

### Step 4 — 归因

拿到 `comparison.md` 后按下面顺序拆解差距：

1. 全体乘 `223/230 = 0.9696` 折掉时钟差，剩下的才是架构差。
2. 把剩余比值对 **clause 数** 和 **Table 5 里 Propagation 占比高的类别（QG / SAT-CMP）**
   做散点。如果访存密集的题比访存轻的题慢得更多 → 确认是 NoC 单端口的锅。
3. 覆盖率单独统计：59 条里有多少条因 `-4`/`-2` 解不出来。这条**不要**混进速度比值里。

### Step 5 — （可选）补 CPU 基线

论文的 MiniSat/Kissat 数字跑在 AMD EPYC 7V13 @ 2.5 GHz。如果本地 CPU 不同，
论文那两列只能当参考、不能当本机对照。要做端到端结论就得在本机重跑 MiniSat + Kissat：

```bash
# 参考仓库 README 的 "To run MiniSat or Kissat" 一节
```

---

## 5. 对比结果 — 已解出

<!-- 粘贴 comparison.md 的第一张表 -->

_待 Step 3 产出_

## 6. 对比结果 — 未解出

<!-- 粘贴 comparison.md 的第二张表 -->

_待 Step 3 产出_

---

## 7. 风险与已知坑

| # | 风险 | 说明 | 缓解 |
|---|---|---|---|
| 1 | 覆盖率可能不足 | 静态审计已确认 59/59 全部装得下（§3），**不会在加载阶段被拒**。但余量最紧的 5 条（`16_8_7` 余 36k、`16_8_6` 余 110k…）在搜索中仍可能耗尽学习子句 page 而 `-4` | 先跑这 5 条当探针；若 `-4` 数量多，§4 的比值统计会有偏（存活下来的都是简单题），必须在结论里写明覆盖率 |
| 2 | 论文口径不明 | 论文没写重复次数、没写是取 min 还是 median | 我们统一报 median 并同时给 min/max，让读者自己判断 |
| 3 | `-4` 与 UNSAT 在 CSV 里都写 `0` | `host.cpp:685` 对两种情况写同一个 result 字段 | 必须靠 `run_status.csv` 的退出码区分；这也是 `summary.md` 自己列的待办项 |
| 4 | `answers.txt` / metrics 是追加写 | 重复跑会累积旧数据 | `run_paper_suite.sh` 每轮 sweep 前会清空对应文件 |
| 5 | 多卡机器选错卡 | | 已用 `FPGA_DEVICE_NAME=vck5000` 环境变量约束 |

---

## 8. 后续设计工作（来自 summary.md，按收益排序）

1. **拆分 literal store 与 clause store 的容量常量**。现在 `_HOST_MAX_CLAUSE_ELEMENTS
   = _HOST_MAX_LITERAL_ELEMENTS`，两者共用一个宏。而仓库 78 个测例的中位数是
   literal 13,728 / clause 9,772（上限均 524,288）——literal store 严重浪费。
   拆开后把 URAM 倾斜给学习子句，是解决 `-4` 最直接的办法。
2. **抬高 `_FPGA_MAX_LEARN_ELE`（1024）** 并测量 BRAM/URAM 与时序代价，解决 `-2`。
3. **量化 NoC 单端口的代价**：把 `k2k_vck5000.cfg` 里的 buffer 试着分到不同 NoC
   端口（若该 platform 暴露多个），对照 §4 Step 4 的散点验证。
4. **给 host CSV 加显式的 resource-exhausted 结果码**，别再用占位 `0`。

---

## 9. 变更记录

| 日期 | 变更 | 人 |
|---|---|---|
| 2026-07-15 | VCK5000 首轮 benchmark（8 个现代测例），1/8 跑通 | |
| 2026-07-23 | 建立本追踪文档；转录论文 59 条基线；补 `run_paper_suite.sh` / `compare_to_paper.py` | |
