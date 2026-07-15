# SAT Competition benchmark

Measured VCK5000 results and raw logs are in
[`results/summary.md`](results/summary.md). The reduced-difficulty 2003 Random
Track instance completes successfully; modern difficult instances expose the
current hardware resource limits.

## rphp4_065_shuffled

- Track: SAT Competition 2021 Main Track
- Family: relativized pigeon-hole
- Expected result: `UNSAT` (`0` for this project's host program)
- DIMACS size: 520 variables, 6,959 clauses
- Global Benchmark Database ID: `0041051c73dcdd885d412a38e8b09fba`
- Official benchmark download: <https://benchmark-database.de/file/0041051c73dcdd885d412a38e8b09fba?context=cnf>
- Official 2021 results: <https://satcompetition.github.io/2021/results/r_main.csv>

This is a deliberately small but difficult instance. In the official 2021
results, 26 sequential solver configurations returned an answer in 201.636 to
2,125.280 seconds (median 887.127 seconds), while 20 configurations timed out.
Its size is safely below the current VCK5000 limits of 32,768 variables,
131,072 clauses, and 524,288 literal/clause-store elements.

Files:

- `rphp4_065_shuffled.cnf.xz`: original downloaded archive
- `rphp4_065_shuffled.cnf`: decompressed DIMACS input
- `results/`: VCK5000 metrics and console logs

Checksums:

```text
SHA256 (compressed)   14ca578f121d396cd6c224fc389e6e3096d5c0ec1744b521066db56549adc6be
SHA256 (decompressed) 1ac28feac4f8f88bc762978fcc4d535fb62a291a1fc9f03e45ffffe2be87f52d
```

Run from the repository root:

```sh
TIMEOUT_SECONDS=1800 ./benchmark/run_vck5000.sh
```

The repository-default configuration can exhaust the VCK5000's reduced
learned-clause storage on this instance. A more aggressive runtime-only
configuration is included; it does not require rebuilding the xclbin:

```sh
CONFIG_FILE="$PWD/benchmark/configuration_vck5000_aggressive.json" \
TIMEOUT_SECONDS=1800 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/rphp4_065_vck5000_aggressive_metrics.csv"
```

## sp4-33-bin-nons-flat-noid

- Track: SAT Competition 2021 Main Track
- Family: minimal superpermutation
- Expected result: `UNSAT`
- DIMACS size: 786 variables, 5,793 clauses
- Global Benchmark Database ID: `0107f34364dfff8ab062193b2c71b4de`
- Official benchmark download: <https://benchmark-database.de/file/0107f34364dfff8ab062193b2c71b4de?context=cnf>

In the official results, 44 sequential configurations solved this instance in
71.430 to 3,384.660 seconds (median 191.678 seconds).

```sh
CNF_FILE="$PWD/benchmark/sp4-33-bin-nons-flat-noid.cnf" \
TIMEOUT_SECONDS=1800 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/sp4-33_vck5000_metrics.csv"
```

Checksums:

```text
SHA256 (compressed)   3bde44695e4e5b54d6931ec5bc2f9da6f305fc08ec38c7cbf767d5a9297f6318
SHA256 (decompressed) e56c30a19f7ca0f8d0f40b782f12bed7b70b009f3de15e7c991ebc60c55c4f4c
```

## quad_res_r29_m32

- Track: SAT Competition 2021 Main Track
- Expected result: `SAT`
- DIMACS size: 5,740 variables, 29,910 clauses
- Global Benchmark Database ID: `198f94ece5fdc9536a28afc3063bdcf6`
- Official benchmark download: <https://benchmark-database.de/file/198f94ece5fdc9536a28afc3063bdcf6?context=cnf>

This is the hardest SAT-labelled candidate in the official results by solved
count: only one of 46 sequential configurations returned SAT (3,541.680
seconds), while 45 timed out.

```sh
CNF_FILE="$PWD/benchmark/quad_res_r29_m32.cnf" \
EXPECTED_RESULT=1 \
TIMEOUT_SECONDS=1800 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/quad_res_r29_m32_vck5000_metrics.csv"
```

Checksums:

```text
SHA256 (compressed)   4504fb4aad4654526df6bb1513db8af4caf2f40a63fa956b9a286bde8aa61e55
SHA256 (decompressed) ab506113f83f86fc4ece33fd8c937dc49cf383b4bf2e181b9020936c92a9728b
```

## sp5-26-19-bin-nons-tree-noid

- Track: SAT Competition 2021 Main Track
- Expected result: `SAT`
- DIMACS size: 11,427 variables, 33,452 clauses
- Global Benchmark Database ID: `eb09e5484eaf494d6c1abe60c5b38b7e`
- Official benchmark download: <https://benchmark-database.de/file/eb09e5484eaf494d6c1abe60c5b38b7e?context=cnf>

In the official results, 35 of 46 sequential configurations timed out. The
fastest solved run took 98.923 seconds and the slowest took 4,192.070 seconds.

```sh
CNF_FILE="$PWD/benchmark/sp5-26-19-bin-nons-tree-noid.cnf" \
EXPECTED_RESULT=1 \
TIMEOUT_SECONDS=1800 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/sp5-26-19_vck5000_metrics.csv"
```

Checksums:

```text
SHA256 (compressed)   06b749a8fbb672cdb23376789531a8ba5b3c5cff7c2a57e9e43be0bcecfd5def
SHA256 (decompressed) d833de04e37f7521b3c086f00bd4fafec111718e6d91f4efa05412b96f45d8ea
```

## randomG-B-Mix-n15-d05

- Track: SAT Competition 2021 Main Track
- Expected result: `UNSAT`
- DIMACS size: 342 variables, 738 clauses
- Global Benchmark Database ID: `747955bb7addf0fb6fe4d465a9cbd035`
- Official benchmark download: <https://benchmark-database.de/file/747955bb7addf0fb6fe4d465a9cbd035?context=cnf>

This is the smaller, medium-difficulty test. All 44 verified sequential results
finished in 54.266 to 559.928 seconds (median 130.020 seconds).

```sh
CNF_FILE="$PWD/benchmark/randomG-B-Mix-n15-d05.cnf" \
EXPECTED_RESULT=0 \
TIMEOUT_SECONDS=1800 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/randomG-B-Mix-n15-d05_vck5000_metrics.csv"
```

Checksums:

```text
SHA256 (compressed)   5b752774226e5db6ee07b5bd7cd8630c69b3ad5b994816601efd5cd841d270dd
SHA256 (decompressed) 0ff014fa8b4cfeff549c0d22f2d042dcb0f01a8bbad7f92e538aa42bfb904449
```

## hidden-k3-s0-r4-n500-03

- Track: SAT Competition 2003 Random Track
- Family: random hidden-model 3-SAT
- Expected result: `SAT`
- DIMACS size: 500 variables, 2,000 clauses
- Global Benchmark Database ID: `03c7c131fc68f902620c24f5f28cd179`
- Official benchmark download: <https://benchmark-database.de/file/03c7c131fc68f902620c24f5f28cd179?context=cnf>

This older competition instance is the reduced-difficulty VCK5000 functional
performance test. It is large enough to exercise conflict learning but small
enough to stay away from the modern Main Track resource failures above.

```sh
CNF_FILE="$PWD/benchmark/hidden-k3-s0-r4-n500-03.cnf" \
EXPECTED_RESULT=1 \
TIMEOUT_SECONDS=1800 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/hidden-k3-s0-r4-n500-03_vck5000_metrics.csv"
```

Checksums:

```text
SHA256 (compressed)   58ce2b5c32dfce073bf775c0f4f934602ae8dfafa38ba1db4397e31b3e08d6bd
SHA256 (decompressed) 2825bc1159f5583edf43935cd380c9d49036c79f5d6e8116556b41dd0b563df2
```

## unif-r3-v500-c1500-01

- Track: SAT Competition 2003 Random Track
- Family: uniform random 3-SAT
- Expected result: `SAT`
- DIMACS size: 500 variables, 1,500 clauses (clause/variable ratio 3.0)
- Global Benchmark Database ID: `dd871dcfc8b837cd848d253dff26a478`
- Official benchmark download: <https://benchmark-database.de/file/dd871dcfc8b837cd848d253dff26a478?context=cnf>

This is the reduced-difficulty competition test. Its density is below the
random 3-SAT phase-transition region, so it has a larger satisfying search
space than `hidden-k3-s0-r4-n500-03`.

```sh
CNF_FILE="$PWD/benchmark/unif-r3-v500-c1500-01.cnf" \
EXPECTED_RESULT=1 \
TIMEOUT_SECONDS=1800 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/unif-r3-v500-c1500-01_vck5000_metrics.csv"
```

Checksums:

```text
SHA256 (compressed)   9121ee9f5646082a0d4d0f812247e62751b25a8508c7a90fb75e8d6418854b75
SHA256 (decompressed) 9775c374b33246a5be13e9c03c4527f70ffeb99100d33ddf372a46a3007fd0f9
```
