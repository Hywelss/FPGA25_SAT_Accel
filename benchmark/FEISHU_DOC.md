# VCK5000 移植 vs. SAT-Accel 论文 — 对齐追踪

> 全选本文件内容 → 复制 → 粘贴进飞书文档，飞书会自动转换标题 / 表格 / 任务列表。
> 单条技术细节见仓库 `benchmark/TRACKING_vck5000_vs_paper.md`，本文件是给人看的追踪面板。

| | |
|---|---|
| 论文 | Lo, Chang, Cong. *SAT-Accel: A Modern SAT Solver on a FPGA.* FPGA '25, pp. 234–246 |
| 代码 | `FORMiND-Lab/FPGA25_SAT_Accel`，`main` = 论文 artifact，`vck5000-paper-comparison` = 本次对比分支 |
| 状态 | 🟡 **待测** — 板端数据尚未采集 |
| 最后更新 | 2026-07-23 |

---

## 一、结论（当前）

**现在给不出"差距多少"的数字。**

已经在 VCK5000 上跑过的 8 个测例，与论文 Table 3 / Table 4 的 59 个测例**没有任何一条重合**，
两组数据不可比。而且那 8 条里有 7 条是撞资源上限退出的，记录的是"撞限制的时间"，不是求解时间。

要拿到数字，必须在 VCK5000 上重跑论文表格里的那 59 条。工具已经就位，剩下的是占板子跑一遍。

---

## 二、平台对照

| 项 | 论文（U55C） | 本次移植（VCK5000） | 影响 |
|---|---|---|---|
| 板卡 | Alveo U55C（VU47P，16 nm） | VCK5000（VC1902 Versal） | — |
| Platform | `xilinx_u55c_gen3x16_xdma_3_202210_1` | `xilinx_vck5000_gen4x8_qdma_2_202220_1` | — |
| 数据时钟 | **230 MHz** | **223 MHz** | ≈ 3% 慢，可直接折算 |
| Vitis / XRT | 2022.2 / 2.14.384 | 2022.2 / 2.14.354 | 无 |
| URAM 总量 | 960（论文用 778 = 81%） | **463** | 装不下原设计 |
| 片外存储 | HBM，9 buffer 分散在 6 channel | 单个 `MC_NOC0`（DDR over NoC） | bank 并行度丢失，影响未量化 |
| `_FPGA_MAX_LITERAL_ELEMENTS` | 1,048,576 | **524,288（减半）** | literal/clause store 与 page 池同时减半 |
| `_FPGA_MAX_LEARN_ELE` | 1,024 | 1,024（未改） | 两边一致，非移植引入 |
| 求解器算法代码 | — | **一行未改** | 差异只可能来自容量与访存 |

预期差距有三个来源，必须分开归因：

1. **时钟** 230 → 223 MHz：确定的 ~3%，对所有测例一致。
2. **访存拓扑** HBM 多 bank → NoC 单端口：论文 Table 5 显示 Propagation 占 29–45% 的时间、是首要瓶颈，
   所以这一项**可能是主要差距来源**。
3. **容量减半**：不影响解得出的题的速度，只决定**哪些题解不出来**（覆盖率）。

---

## 三、比较范围 —— 只比论文表格里的数字

| 论文位置 | 数字 | 本次是否复现 |
|---|---|---|
| Table 4 每行 `Time in ms / SA` 列 | 46 条 SAT-Accel 单条时间 | ✅ 逐条对比 |
| Table 3 每行 `Time in ms (SA)` 列 | 13 条 SAT-Accel 单条时间 | ✅ 逐条对比 |
| Table 4 汇总行 `SA Speedup Avg` | vs MiniSat **17.86x**、vs Kissat **2.77x** | ⬜ 需本机重跑 MiniSat/Kissat（Step 5） |
| Table 3 汇总行 `SA Spdup Avg` | vs SAT-Hard **800** | ❌ 不复现，手上没有 SAT-Hard |

**明确不做的对比：**

- Table 4 的 MiniSat / Kissat 两列跑在 EPYC 7V13 @ 2.5 GHz，不是本机 CPU，不与本机任何数字相比。
- Table 4 的 co-processor 估算列（0.3 µs / 1 µs / 160 µs / MS Prop. Cnt）是论文的模型外推，不是实测。
- Table 2（资源占用）是 U55C 的综合结果，与 VCK5000 布局布线无可比性。
- Table 5（阶段时间占比）只用于归因方向，不当作对比项。
- 2026-07-15 那 8 个现代测例不在论文任何表格里，不参与对比。

⚠️ Table 3 第二列论文标的是 `Var`，Table 4 标的是 `Literals`；`paper_baseline.csv` 里都写进
`paper_lits` 字段，读的时候注意口径不同。

---

## 四、静态容量审计（已完成，无需板卡）

在 VCK5000 的 524,288 上限下复刻 `host.cpp` 的分配算术：**59 / 59 全部装得下，0 条被拒**。
即没有任何一条会在 kernel 启动前被 host 拒绝。

clause store 余量最紧的 5 条，是搜索中耗尽学习子句 page（`-4`）的高风险项，应优先拿它们探路：

| 测例 | clause store | literal store | 剩余余量 |
|---|---:|---:|---:|
| `16_8_7` | 93.1% | 76.8% | 36,072 |
| `16_8_6` | 79.0% | 65.4% | 110,084 |
| `16_16_3` | 75.8% | 68.2% | 126,636 |
| `logistics-rotate-07t5` | 67.7% | 51.8% | 169,356 |
| `16_8_5` | 64.9% | 54.0% | 183,972 |

⚠️ **静态装得下 ≠ 解得出来。**

---

## 五、实施步骤

- [ ] **Step 0 — 前置检查**（板端，~5 min）确认卡在位、xclbin 是 VCK5000 build、记录实际数据时钟
- [ ] **Step 1 — 编译 host**（板端，~2 min）`./runCompile.sh opencl`，必须带 `-DFPGA_VCK5000`
- [x] **Step 1.5 — 静态容量审计**（任意机器，~10 s）已完成，59/59 装得下
- [ ] **Step 2 — 跑论文 59 条测例**（板端，~1–3 h）`REPEATS=5 ./benchmark/run_paper_suite.sh`
- [ ] **Step 3 — 生成对比表**（任意机器，~1 s）`python3 benchmark/compare_to_paper.py`
- [ ] **Step 4 — 归因**：先折掉 3% 时钟差，再看剩余比值与访存密集度的相关性
- [ ] **Step 5 —（可选）补 CPU 基线**：本机重跑 MiniSat + Kissat，才能对 Table 4 的汇总行

命令细节见仓库 `benchmark/TRACKING_vck5000_vs_paper.md` §4。

---

## 六、对比结果 — 已解出

_待 Step 3 产出。把 `benchmark/results/paper_suite/comparison.md` 的第一张表粘贴到这里。_

## 七、对比结果 — 未解出

_待 Step 3 产出。把 `comparison.md` 的第二张表粘贴到这里。_

---

## 八、读数时必须知道的四件事

1. **取哪一列**：host metrics CSV 第 5 列（index 4）是 kernel 执行时间（秒），来源
   `host.cpp:780` 的 `executionTime/1e9`。这是与论文 "Time in ms" 口径一致的列。
   仓库 README 里说的"Column J 是 FPGA runtime"是错的，J 列是 cycle counter 百分比。
2. **论文的有效数字**：论文的 SAT-Accel 时间只印 1–2 位有效数字，印出来的 `1` 实际在 [0.5, 1.5)。
   对这种值算比值，光是舍入就带 ~50% 误差。所以汇总要报两次：全体、以及只取论文时间 ≥ 10 ms 的子集，
   **后者才是能用的那个**。
3. **`-4` 和 UNSAT 在 CSV 里都写 `0`**（`host.cpp:685` 用同一个字段），必须靠 `run_status.csv`
   的退出码区分，否则会把"跑挂了"当成"解出来是 UNSAT"。
4. **覆盖率单独统计**，不要混进速度比值里。59 条里有多少条因 `-4` / `-2` 解不出来，是独立的一个结论。

---

## 九、风险

| # | 风险 | 说明 | 缓解 |
|---|---|---|---|
| 1 | 覆盖率不足 | 静态 59/59 装得下，但余量最紧的 5 条搜索中仍可能 `-4` | 先跑这 5 条探路；若 `-4` 多，速度比值会有偏（存活的都是简单题），结论里必须写明覆盖率 |
| 2 | 论文口径不明 | 论文没写重复次数、没写取 min 还是 median | 统一报 min 并同时给 median / spread |
| 3 | 搜索路径改变 | 容量减半改变了子句删除时机 → VSIDS 分数 → 决策序列 | 比值明显偏离 1 且方差小的条目单列一张表，排除出速度结论 |
| 4 | 结果文件追加写 | 重复跑会累积旧数据 | `run_paper_suite.sh` 每轮 sweep 前清空 |
| 5 | 多卡机器选错卡 | | 已用 `FPGA_DEVICE_NAME=vck5000` 约束 |

---

## 十、变更记录

| 日期 | 变更 |
|---|---|
| 2026-07-15 | VCK5000 首轮 benchmark（8 个现代测例），1/8 跑通 |
| 2026-07-23 | 建立追踪；转录论文 59 条基线并逐条核对原文；补对比脚本；建立本飞书文档 |
