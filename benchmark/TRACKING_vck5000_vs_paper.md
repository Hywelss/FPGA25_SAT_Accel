# VCK5000 移植 vs. SAT-Accel 论文 — 对齐追踪

> 论文：Lo, Chang, Cong. *SAT-Accel: A Modern SAT Solver on a FPGA.* FPGA '25, pp. 234–246.
> 代码：`FORMiND-Lab/FPGA25_SAT_Accel`，`main` = 论文 artifact，`vck5000-adaptation` = 本次移植。
> 文档状态：**已测**（2026-07-23 完成 59 条 × 5 次全量 sweep）
> 最后更新：2026-07-23

---

## 0. 一句话结论

**本板比论文慢约 25%**：3% 来自时钟（230 → 223 MHz），21% 来自架构——同样的活多花 21% 的周期。

依据是论文 Table 3/Table 4 里 59 条测例中，**论文时间 ≥ 10 ms 且两边工作量可比的 11 条**。
两条独立路径互相印证：时间比中位数 1.26x，周期比中位数 1.21x，而
1.205 × (230/223) = 1.243，与实测的 1.263 差 1.6%。

那 21% 的架构开销定位到了 Propagation：复现论文 Table 5 后，7 个类别里有 6 个的
Propagation 周期占比上升，中位数 +7.7 个百分点（§5.3）。这与 HBM 多 bank 换成
NoC 单端口的预期一致。

三条附带结论：

- **覆盖率 55/59。** 4 条未解出，其中 3 条是论文最慢的题（容量减半的代价），
  1 条论文在 U55C 上也没解出。
- **实例身份已验证。** Table 4 的 Literals / Clauses 两列 59/59 逐字对上，
  排除了"跑的不是同一批文件"这个可能。
- **有 3 条本板确实在跑不同的搜索**（`4blocks` 只花论文隐含周期的 15%），
  已从上面的比值里剔除，机制待查（§7 风险 3）。

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

论文 Table 4（46 例）+ Table 3（13 例，与 SAT-Hard 对比）= **59 条基线**，
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

⚠️ **静态装得下 ≠ 解得出来**。剩余余量是留给学习子句的动态 page 池，搜索过程中仍可能耗尽。

⚠️ **但静态余量不是 OOM 的预测指标。** 实测证伪：静态占 93.1% 的 `16_8_7` 跑通了，
静态占 67.7%、还剩 169k 的 `logistics-rotate` 反而爆了。原因是静态占用只决定初始子句，
而 OOM 是动态学习子句撑爆 page 池，决定学多少子句的是**搜索深度**——
看论文 SA 时间就够了（`16_8_7` 论文 1 ms 几乎不学子句，`logistics-rotate` 论文 1088 ms）。
实测 4 条未解出的里有 3 条正是论文最慢的 3 条。详见 §7 风险 2。

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

### 比较范围（只比论文表格，不做其他对比）

对比只针对论文 Table 3 / Table 4 里印出来的数字，逐条一一对应。已核对原文：

| 论文位置 | 数字 | 本次是否复现 |
|---|---|---|
| Table 4 每行 "Time in ms / SA" 列 | 46 条 SAT-Accel 单条时间 | ✅ 逐条对比（本文档 §5） |
| Table 3 每行 "Time in ms (SA)" 列 | 13 条 SAT-Accel 单条时间 | ✅ 逐条对比（本文档 §5） |
| Table 4 汇总行 `SA Speedup Avg` | vs MiniSat **17.86x**、vs Kissat **2.77x** | ⬜ 只有本机重跑 MiniSat/Kissat 才能复现（Step 5） |
| Table 3 汇总行 `SA Spdup Avg` | vs SAT-Hard **800** | ❌ 不复现，手上没有 SAT-Hard |

**明确不做的对比**（避免得出无意义的结论）：

- Table 4 的 `MiniSat` / `Kissat` 两列跑在 EPYC 7V13 @ 2.5 GHz 上，不是本机 CPU，
  不与本机任何数字相比。
- Table 4 的 co-processor 估算列（0.3 µs / 1 µs / 160 µs / MS Prop. Cnt）是论文的
  **模型外推**，不是实测，不参与对比。
- Table 2（资源占用）是 U55C 的综合结果，与 VCK5000 的布局布线无可比性；
  URAM 数字只在 §1 里作为「为什么容量减半」的说明，不当作对比项。
- Table 5（各阶段时间占比）只用于 §4 Step 4 的归因方向判断，不当作对比项。
- 2026-07-15 那 8 个现代测例（§2）不在论文任何表格里，不参与对比。

⚠️ Table 3 的第二列论文标的是 `Var`（变量数），Table 4 标的是 `Literals`。
`paper_baseline.csv` 里两者都写进 `paper_lits` 字段，读的时候注意口径不同。

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

## 5. 对比结果

采集于 2026-07-23，`formind-lab-server` 上的 VCK5000 @ 223 MHz DATA_CLK，
59 条 × 5 次重复，默认配置（prune 10% / reset 100 / decay 0.95 / page 16-4，
与论文 §6 所述配置逐项一致）。原始数据在 `results/paper_suite/`。

时间一律取 5 次重复的**最小值**：求解器是确定性的，同一实例同一配置重放同一条搜索，
重复间的差异是测量干扰（排队、争用）而不是求解器行为，最小值受污染最少。
实测周期数跨 5 次重复的最大跨度是 1.086，基本确定性。

### 5.1 计时口径已验证

`totalCycleCount ÷ 223 MHz` 对实测 kernel 时间，运行时间 > 10 ms 的测例全部落在
**0.97–0.996**。kernel 时间就是周期数除以数据时钟，中间没有夹带别的东西。
短测例（< 1 ms）这个比值掉到 0.34–0.7，是固定启动开销占比变大，符合预期。

### 5.2 Table 4 / Table 3 逐条

完整 59 行（含 Literals / Clauses 两列的逐条核对）见
`results/paper_suite/table4_cols.md`，由 `benchmark/table4_cols.py` 生成。汇总：

| | |
|---|---|
| Literals / Clauses 对上 | **59 / 59**，零偏差 |
| SA 时间解出 | 55 / 59 |
| 时间比中位数（论文 ≥ 10 ms，14 条） | 1.25x |
| 时间比中位数（其中工作量可比的 11 条） | **1.26x** |
| 周期比中位数（同 11 条） | **1.21x** |
| 交叉校验 | 1.205 × (230/223) = 1.243 vs 实测 1.263，差 1.6% |

论文时间 < 10 ms 的行不进聚合：论文只印 1–2 位有效数字，印出来的 `1` 实际在
[0.5, 1.5)，比值光靠舍入就带 ±50% 误差。

### 5.3 Table 5 复现（10% 删除阈值）

阶段占比是每次运行**内部归一化**的，不受搜索路径长短影响，是这批数据里最干净的对比。
由 `benchmark/table5_compare.py` 从 metrics CSV 的 cycle counter 列重建，无需额外插桩。

计数器到论文 7 列的映射被唯一确定：`host.cpp` 的 9 个计数器里 `BACKTRACK` 全类别恒为
0.00（硬件从未递增），剩 8 个映射到 7 列，只有 `Min|Bktrk` 是 `LEARN_MIN + SAVE` 的和。
`Allocate`(RESIZE) 与 `Delete`(DELETE) 两列偏差 < 1 pp，反证映射正确。

| 类别 | | Load | Decide | **Propagate** | Learn | Min\|Bktrk | Allocate | Delete |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| SAT-CMP | 论文 | 0.04 | 2.59 | *42.25* | 13.25 | 39.10 | 0.33 | 2.45 |
| | 实测 | 0.00 | 0.58 | **47.98** | 8.69 | 40.82 | 0.25 | 1.68 |
| | 差 | −0.04 | −2.01 | **+5.73** | −4.56 | +1.72 | −0.08 | −0.77 |
| IBM | 论文 | 4.01 | 25.02 | *39.51* | 9.05 | 22.16 | 0.11 | 0.14 |
| | 实测 | 0.82 | 7.45 | **47.17** | 12.71 | 31.08 | 0.13 | 0.64 |
| | 差 | −3.19 | −17.57 | **+7.66** | +3.66 | +8.92 | +0.02 | +0.50 |
| SSA | 论文 | 24.63 | 11.65 | *29.65* | 18.40 | 15.39 | 0.16 | 0.11 |
| | 实测 | 3.62 | 5.41 | **30.41** | 29.90 | 30.10 | 0.22 | 0.34 |
| | 差 | −21.01 | −6.24 | **+0.76** | +11.50 | +14.71 | +0.06 | +0.23 |
| QG | 论文 | 3.76 | 0.95 | *45.25* | 19.91 | 29.48 | 0.25 | 0.40 |
| | 实测 | 0.08 | 0.32 | **56.25** | 12.22 | 30.31 | 0.13 | 0.70 |
| | 差 | −3.68 | −0.63 | **+11.00** | −7.69 | +0.83 | −0.12 | +0.30 |
| PUZZLE | 论文 | 10.10 | 6.37 | *37.34* | 11.75 | 33.81 | 0.18 | 0.46 |
| | 实测 | 2.77 | 1.55 | **15.33** | 8.02 | 71.89 | 0.19 | 0.25 |
| | 差 | −7.33 | −4.82 | **−22.01** | −3.73 | +38.08 | +0.01 | −0.21 |
| PLAN | 论文 | 1.33 | 13.44 | *38.20* | 15.44 | 31.02 | 0.32 | 0.25 |
| | 实测 | 1.61 | 6.12 | **47.66** | 13.48 | 30.73 | 0.20 | 0.20 |
| | 差 | +0.28 | −7.32 | **+9.46** | −1.96 | −0.29 | −0.12 | −0.05 |
| QTMCKT | 论文 | 21.03 | 2.91 | *33.34* | 20.99 | 20.97 | 0.44 | 0.32 |
| | 实测 | 7.90 | 3.63 | **41.27** | 20.89 | 25.48 | 0.47 | 0.37 |
| | 差 | −13.13 | +0.72 | **+7.93** | −0.10 | +4.51 | +0.03 | +0.05 |

**Propagation 占比 7 个类别里 6 个上升，中位数 +7.7 pp**（QG +11.0、PLAN +9.5、
QTMCKT +7.9、IBM +7.7、SAT-CMP +5.7、SSA +0.8）。这是 NoC 单端口归因的**直接测量**，
不再依赖"慢的题恰好访存重"这种间接推断。

唯一反向的 PUZZLE 只有 3 条，按周期加权后基本等于 `nqueens_32` 一条，权重太小。

两个副产物：

- **Load 占比全线下降**（SSA 24.63 → 3.62、QTMCKT 21.03 → 7.90）。
  合理解释是 `_FPGA_MAX_LITERAL_ELEMENTS` 减半后要初始化的存储量也减半，装载变快——
  容量减半的一个正面副作用。
- **Decide 占比全线下降**（IBM 25.02 → 7.45）。PQ 走 BRAM/URAM 不碰 DRAM，
  绝对开销没变，是分母被 Propagation 撑大了。

绝对周期倍数也算过，但被 §5.5 的搜索路径差异污染（PLAN 总周期只有论文的 0.37 倍），
不可用，所以只报占比。

### 5.4 Table 5 的 90% 列 — 未复现

论文那半张表用来支撑"把删除比例从 10% 提到 90%，开销没有明显增加"这个结论。
数值已录进 `table5_compare.py` 的 `PAPER_90`，但复现需要另建
`_HOST_PRUNE_PERCENTAGE = 0.9` 的配置再跑一遍全量（现成的 aggressive 配置是 0.5，
对不上）。跑完用 `PRUNE=90 python3 benchmark/table5_compare.py` 对照。

### 5.5 有 3 条本板在跑不同的搜索

| 测例 | 论文 | 本板 | 周期比 |
|---|---:|---:|---:|
| `4blocks` | 69 ms | 11.411 ms | **0.15** |
| `blocksworldc` | 25 ms | 13.325 ms | 0.51 |
| `hole7_unsat` | 125 ms | 74.963 ms | 0.56 |

时钟更低、访存更窄，不可能少花 6.7 倍周期；论文这几个数是 2 位有效数字，
舍入解释不了 69 → 11.4。所以本板确实干的活更少，测的是不同工作量，已从 §5.2 剔除。

按论文 §5.9，restart 由学到的子句数触发、clause deletion 挂在 restart 上，
都由配置驱动、与存储容量无关；配置又与论文逐项一致，算法代码未改。
所以本不该有差异——机制待查，见 §7 风险 3。

## 6. 覆盖率 — 4 条未解出

| 测例 | 论文 SA | 说明 |
|---|---:|---|
| `marg3x3add8ch` | 6,387 ms | OOM（退出码 3），Table 4 最慢的一条 |
| `logistics-rotate-07t5` | 1,088 ms | OOM |
| `289-sat-6x30` | 556 ms | OOM |
| `hole9_unsat` | N/A | 论文在 U55C 上也 OOM，两边都没有 |

真正因容量减半丢掉的是 3 条，**全部落在论文最慢的那一档**。
所以 §5.2 的"慢 25%"覆盖不到论文最难的题，引用时必须一并说明。

---

## 7. 风险与已知坑

| # | 风险 | 说明 | 状态 |
|---|---|---|---|
| 1 | 覆盖率不足 | 实测 55/59，丢的 3 条全在论文最慢那一档 | ⚠️ 已确认存在。§5.2 的比值不覆盖论文最难的题，引用时必须一并说明 |
| 2 | ~~静态余量最紧的测例风险最高~~ | **这个判断是错的。** 静态占用只决定初始子句放不放得下，而 OOM 是动态学习子句撑爆 page 池，决定学多少子句的是**搜索深度**。实测：静态占 93.1% 的 `16_8_7`（论文 1 ms）跑通，静态占 67.7%、余 169k 的 `logistics-rotate`（论文 1088 ms）爆了 | ✅ 已纠正。**真正的预测指标是论文 SA 时间**——4 条未解出的里 3 条是论文最慢的 3 条 |
| 3 | 搜索路径差异 | 3 条测例的周期数远低于论文时间隐含的值（`4blocks` 只有 15%），说明本板跑的不是同一条搜索。但按论文 §5.9，删除由 restart 驱动、与容量无关，配置也逐项一致，本不该有差异 | ❓ **机制未知，待查**。已从 §5.2 的比值里剔除。下一步：查 `_FPGA_MAX_LITERAL_ELEMENTS` 减半是否影响了子句删除时挑哪些子句（若选择依赖物理页地址，池子减半就会换一批） |
| 4 | 论文口径不明 | 论文没写重复次数、没写取 min 还是 median | ✅ 统一报 min（求解器确定性，跨重复的差异是测量干扰），同时给 median 与跨度让读者判断 |
| 5 | OOM 与 UNSAT 在 metrics CSV 里都写 `0` | `host.cpp:705` 对两种情况写同一个 result 字段 | ✅ 靠 `run_status.csv` 的退出码区分（3 = OOM，4 = 答案错误，0 = 解出） |
| 6 | `run_status.csv` 每次 sweep 从头重写 | `run_paper_suite.sh` 启动时 `> "$STATUS_FILE"`，任何带 `ONLY=` 的筛选跑都会抹掉不在本轮里的测例状态，而各自的 `.csv` 计时却保留，造成"有时间数据但状态是 not run"的错配 | ⚠️ **未修**。当前只能一次跑全。改成追加 + 去重即可支持分批 |
| 7 | metrics 是追加写 | 重复跑会累积旧数据 | ✅ `run_paper_suite.sh` 每条测例 sweep 前清空对应文件 |
| 8 | 多卡机器选错卡 | | ✅ 已用 `FPGA_DEVICE_NAME=vck5000` 约束 |

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
| 2026-07-23 | 建立本追踪文档；转录论文 59 条基线并逐条核对 PDF；补 `run_paper_suite.sh` / `compare_to_paper.py` | |
| 2026-07-23 | 在 `formind-lab-server` 完成 59 条 × 5 次全量 sweep；填入 §5 / §6 全部结果；复现论文 Table 5 的 10% 半张；纠正 §3 / §7 关于 OOM 预测指标的错误判断 | |
