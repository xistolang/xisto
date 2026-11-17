---
title: Xisto vs Node
sidebar_position: 3
---

import {SpeedupBars, MemoryBars} from '@site/src/components/Charts/Speedup';

# Xisto vs Node

The same TypeScript file runs on both sides: [`bench/polybench/ts/KERNEL.ts`](https://github.com/xistolang/xisto/tree/main/bench/polybench/ts) is compiled by Xisto and run as is by Node v26.12.0 (`node KERNEL.ts`, which strips the type annotations). So this page compares a native compiler with a JIT on identical source code, with `number[][]` arrays on both sides. Kernel time is PolyBench's own timer around the kernel, the same region in every port; CPU is the whole process's user + system time; memory is peak RSS. LARGE dataset, median of 3 runs. Every run's array dump hashes the same as the C reference's.

The Node runs are from 15 Nov 2026 (Node v26.12.0). The Xisto side is the release that fixes a 4–5× slowdown of `gemm` found in that round (an array-hole check that had moved into the innermost loop): on the Xeon `c53e83d`, on the Graviton2 `c5748f6` (the same arm64 code), measured in the A/B before the merge. Result files: `bench/results/polybench-2026-11-15-js/`.

## Xeon E5-2686 v4 (x86-64, 2 vCPUs, AVX2)

Kernel time: Xisto **30 wins**, 0 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

<SpeedupBars page="pb-vs-node" section="Xeon E5-2686 v4" unit="kernel" />

<MemoryBars page="pb-vs-node" section="Xeon E5-2686 v4" unit="kernel" />

| kernel | C (ref.) kernel | Xisto kernel | Node kernel | ratio | Xisto CPU | Node CPU | Xisto memory | Node memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 2.72 s | **628 ms** | 12.71 s | 0.05 | **0.66 s** | 13.01 s | **37.4 MB** | 203.4 MB | **Xisto** |
| 3mm | 4.22 s | **1.02 s** | 22.62 s | 0.04 | **1.06 s** | 23.00 s | **53.3 MB** | 227.8 MB | **Xisto** |
| adi | 12.50 s | **6.40 s** | 21.50 s | 0.30 | **6.43 s** | 21.79 s | **32.8 MB** | 205.8 MB | **Xisto** |
| atax | 8 ms | **7 ms** | 33 ms | 0.21 | **0.04 s** | 0.27 s | **32.4 MB** | 151.8 MB | **Xisto** |
| bicg | 13 ms | **7 ms** | 23 ms | 0.30 | **0.04 s** | 0.25 s | **32.5 MB** | 152.2 MB | **Xisto** |
| cholesky | 1.99 s | **1.99 s** | 6.55 s | 0.30 | **7.96 s** | 86.18 s | **63.0 MB** | 260.0 MB | **Xisto** |
| correlation | 7.30 s | **731 ms** | 11.54 s | 0.06 | **0.75 s** | 11.76 s | **25.9 MB** | 153.6 MB | **Xisto** |
| covariance | 7.34 s | **726 ms** | 10.84 s | 0.07 | **0.75 s** | 11.04 s | **25.9 MB** | 140.8 MB | **Xisto** |
| deriche | 462 ms | **167 ms** | 1.01 s | 0.16 | **0.27 s** | 1.36 s | **138.4 MB** | 223.5 MB | **Xisto** |
| doitgen | 740 ms | **91 ms** | 2.61 s | 0.03 | **0.12 s** | 2.85 s | **29.0 MB** | 150.0 MB | **Xisto** |
| durbin | 3 ms | **4 ms** | 15 ms | 0.27 | **0.00 s** | 0.13 s | **1.9 MB** | 74.6 MB | **Xisto** |
| fdtd-2d | 1.58 s | **1.76 s** | 6.70 s | 0.26 | **1.78 s** | 7.04 s | **29.5 MB** | 195.3 MB | **Xisto** |
| floyd-warshall | 18.10 s | **11.23 s** | 49.95 s | 0.22 | **11.28 s** | 50.13 s | **32.0 MB** | 101.8 MB | **Xisto** |
| gemm | 416 ms | **436 ms** | 4.37 s | 0.10 | **0.47 s** | 4.65 s | **29.6 MB** | 189.8 MB | **Xisto** |
| gemver | 31 ms | **10 ms** | 101 ms | 0.10 | **0.04 s** | 0.34 s | **32.6 MB** | 163.9 MB | **Xisto** |
| gesummv | 6 ms | **3 ms** | 12 ms | 0.25 | **0.03 s** | 0.23 s | **27.7 MB** | 140.1 MB | **Xisto** |
| gramschmidt | 6.97 s | **1.41 s** | 15.29 s | 0.09 | **1.44 s** | 15.55 s | **31.4 MB** | 192.5 MB | **Xisto** |
| heat-3d | 2.22 s | **1.83 s** | 21.33 s | 0.09 | **1.86 s** | 21.60 s | **31.3 MB** | 170.5 MB | **Xisto** |
| jacobi-1d | 0 ms | **1 ms** | 11 ms | 0.09 | **0.00 s** | 0.13 s | **1.9 MB** | 74.2 MB | **Xisto** |
| jacobi-2d | 1.41 s | **1.32 s** | 9.20 s | 0.14 | **1.34 s** | 9.42 s | **27.8 MB** | 146.0 MB | **Xisto** |
| lu | 5.68 s | **2.33 s** | 24.89 s | 0.09 | **8.43 s** | 124.46 s | **63.1 MB** | 260.5 MB | **Xisto** |
| ludcmp | 5.27 s | **2.33 s** | 16.53 s | 0.14 | **8.29 s** | 122.80 s | **63.2 MB** | 257.7 MB | **Xisto** |
| mvt | 27 ms | **6 ms** | 75 ms | 0.08 | **0.04 s** | 0.30 s | **32.5 MB** | 153.7 MB | **Xisto** |
| nussinov | 4.32 s | **4.11 s** | 14.34 s | 0.29 | **4.13 s** | 14.49 s | **25.9 MB** | 99.2 MB | **Xisto** |
| seidel-2d | 27.68 s | **4.84 s** | 35.90 s | 0.13 | **4.87 s** | 36.11 s | **32.5 MB** | 153.4 MB | **Xisto** |
| symm | 2.07 s | **1.21 s** | 9.10 s | 0.13 | **1.23 s** | 9.34 s | **28.1 MB** | 150.6 MB | **Xisto** |
| syr2k | 4.61 s | **1.13 s** | 12.46 s | 0.09 | **1.16 s** | 12.73 s | **31.4 MB** | 192.7 MB | **Xisto** |
| syrk | 652 ms | **541 ms** | 3.87 s | 0.14 | **0.56 s** | 4.06 s | **22.1 MB** | 133.2 MB | **Xisto** |
| trisolv | 4 ms | **3 ms** | 10 ms | 0.30 | **0.03 s** | 0.23 s | **32.5 MB** | 150.7 MB | **Xisto** |
| trmm | 1.88 s | **266 ms** | 5.90 s | 0.05 | **0.28 s** | 6.13 s | **18.7 MB** | 136.5 MB | **Xisto** |

Source: `bench/results/polybench-2026-11-15-js/polybench-js-ubuntu-xeon-2026-11-15-c53e83d.txt`.

## Graviton2 (Neoverse N1, arm64, 2 vCPUs)

Kernel time: Xisto **30 wins**, 0 ties, 0 losses.

<SpeedupBars page="pb-vs-node" section="Graviton2" unit="kernel" />

<MemoryBars page="pb-vs-node" section="Graviton2" unit="kernel" />

| kernel | C (ref.) kernel | Xisto kernel | Node kernel | ratio | Xisto CPU | Node CPU | Xisto memory | Node memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 6.16 s | **1.78 s** | 26.71 s | 0.07 | **1.81 s** | 27.07 s | **37.0 MB** | 173.4 MB | **Xisto** |
| 3mm | 9.94 s | **2.80 s** | 38.25 s | 0.07 | **2.84 s** | 38.67 s | **53.0 MB** | 199.8 MB | **Xisto** |
| adi | 12.05 s | **10.27 s** | 33.35 s | 0.31 | **10.29 s** | 33.70 s | **32.5 MB** | 176.0 MB | **Xisto** |
| atax | 6 ms | **6 ms** | 32 ms | 0.19 | **0.03 s** | 0.30 s | **31.9 MB** | 167.7 MB | **Xisto** |
| bicg | 14 ms | **4 ms** | 26 ms | 0.15 | **0.03 s** | 0.34 s | **32.0 MB** | 167.4 MB | **Xisto** |
| cholesky | 1.96 s | **1.58 s** | 6.23 s | 0.25 | **13.72 s** | 177.24 s | **62.6 MB** | 229.0 MB | **Xisto** |
| correlation | 7.32 s | **2.13 s** | 19.86 s | 0.11 | **2.15 s** | 20.13 s | **25.4 MB** | 155.3 MB | **Xisto** |
| covariance | 7.36 s | **2.01 s** | 20.22 s | 0.10 | **2.02 s** | 20.50 s | **25.4 MB** | 157.9 MB | **Xisto** |
| deriche | 330 ms | **197 ms** | 1.21 s | 0.16 | **0.28 s** | 1.61 s | **139.3 MB** | 224.7 MB | **Xisto** |
| doitgen | 631 ms | **207 ms** | 3.34 s | 0.06 | **0.23 s** | 3.66 s | **29.3 MB** | 157.9 MB | **Xisto** |
| durbin | 3 ms | **3 ms** | 45 ms | 0.07 | **0.00 s** | 0.21 s | **1.3 MB** | 79.1 MB | **Xisto** |
| fdtd-2d | 1.93 s | **1.92 s** | 11.27 s | 0.17 | **1.95 s** | 11.59 s | **29.2 MB** | 157.7 MB | **Xisto** |
| floyd-warshall | 23.16 s | **14.00 s** | 78.71 s | 0.18 | **14.09 s** | 78.83 s | **31.6 MB** | 103.6 MB | **Xisto** |
| gemm | 679 ms | **647 ms** | 6.91 s | 0.09 | **0.67 s** | 7.23 s | **29.3 MB** | 156.7 MB | **Xisto** |
| gemver | 39 ms | **20 ms** | 106 ms | 0.19 | **0.05 s** | 0.43 s | **32.0 MB** | 172.1 MB | **Xisto** |
| gesummv | 6 ms | **2 ms** | 12 ms | 0.17 | **0.02 s** | 0.27 s | **27.1 MB** | 153.7 MB | **Xisto** |
| gramschmidt | 13.42 s | **2.68 s** | 24.22 s | 0.11 | **2.70 s** | 24.51 s | **30.9 MB** | 170.1 MB | **Xisto** |
| heat-3d | 3.50 s | **3.88 s** | 30.22 s | 0.13 | **3.90 s** | 30.55 s | **30.7 MB** | 167.2 MB | **Xisto** |
| jacobi-1d | 1 ms | **1 ms** | 16 ms | 0.06 | **0.00 s** | 0.16 s | **1.3 MB** | 75.5 MB | **Xisto** |
| jacobi-2d | 1.98 s | **1.96 s** | 13.25 s | 0.15 | **1.98 s** | 13.51 s | **27.3 MB** | 154.4 MB | **Xisto** |
| lu | 12.06 s | **5.40 s** | 45.42 s | 0.12 | **17.64 s** | 161.83 s | **62.7 MB** | 229.2 MB | **Xisto** |
| ludcmp | 10.35 s | **5.03 s** | 31.86 s | 0.16 | **17.06 s** | 179.95 s | **62.6 MB** | 233.6 MB | **Xisto** |
| mvt | 35 ms | **13 ms** | 74 ms | 0.18 | **0.04 s** | 0.39 s | **32.0 MB** | 171.6 MB | **Xisto** |
| nussinov | 9.93 s | **7.59 s** | 26.61 s | 0.29 | **7.60 s** | 26.82 s | **25.4 MB** | 105.1 MB | **Xisto** |
| seidel-2d | 20.60 s | **6.81 s** | 26.03 s | 0.26 | **6.84 s** | 26.30 s | **32.0 MB** | 167.6 MB | **Xisto** |
| symm | 4.55 s | **4.33 s** | 17.96 s | 0.24 | **4.35 s** | 18.24 s | **27.8 MB** | 154.3 MB | **Xisto** |
| syr2k | 5.85 s | **2.90 s** | 21.64 s | 0.13 | **2.92 s** | 21.90 s | **31.1 MB** | 153.1 MB | **Xisto** |
| syrk | 2.11 s | **1.01 s** | 11.94 s | 0.08 | **1.03 s** | 12.19 s | **21.8 MB** | 141.3 MB | **Xisto** |
| trisolv | 3 ms | **2 ms** | 8 ms | 0.25 | **0.02 s** | 0.26 s | **31.9 MB** | 167.5 MB | **Xisto** |
| trmm | 3.85 s | **720 ms** | 10.23 s | 0.07 | **0.73 s** | 10.40 s | **18.2 MB** | 136.4 MB | **Xisto** |

Source: `bench/results/polybench-2026-11-15-js/polybench-js-arm-graviton2-2026-11-15-c5748f6.txt`.
