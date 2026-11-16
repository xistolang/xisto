---
title: Xisto vs Bun
sidebar_position: 4
---

import {SpeedupBars, MemoryBars} from '@site/src/components/Charts/Speedup';

# Xisto vs Bun

The same TypeScript file runs on both sides: [`bench/polybench/ts/KERNEL.ts`](https://github.com/xistolang/xisto/tree/main/bench/polybench/ts) is compiled by Xisto and run as is by Bun 1.4.4 (`bun KERNEL.ts`, which strips the type annotations). So this page compares a native compiler with a JIT on identical source code, with `number[][]` arrays on both sides. Kernel time is PolyBench's own timer around the kernel, the same region in every port; CPU is the whole process's user + system time; memory is peak RSS. LARGE dataset, median of 3 runs. Every run's array dump hashes the same as the C reference's.

The Bun runs are from 15 Nov 2026 (Bun 1.4.4). The Xisto side is the release that fixes a 4–5× slowdown of `gemm` found in that round (an array-hole check that had moved into the innermost loop): on the Xeon `c53e83d`, on the Graviton2 `c5748f6` (the same arm64 code), measured in the A/B before the merge. Result files: `bench/results/polybench-2026-11-15-js/`.

## Xeon E5-2686 v4 (x86-64, 2 vCPUs, AVX2)

Kernel time: Xisto **30 wins**, 0 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

<SpeedupBars page="pb-vs-bun" section="Xeon E5-2686 v4" unit="kernel" />

<MemoryBars page="pb-vs-bun" section="Xeon E5-2686 v4" unit="kernel" />

| kernel | C (ref.) kernel | Xisto kernel | Bun kernel | ratio | Xisto CPU | Bun CPU | Xisto memory | Bun memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 2.72 s | **628 ms** | 12.69 s | 0.05 | **0.66 s** | 12.83 s | **37.4 MB** | 66.6 MB | **Xisto** |
| 3mm | 4.22 s | **1.02 s** | 19.22 s | 0.05 | **1.06 s** | 19.45 s | **53.3 MB** | 86.9 MB | **Xisto** |
| adi | 12.50 s | **6.40 s** | 23.74 s | 0.27 | **6.43 s** | 24.04 s | **32.8 MB** | 62.9 MB | **Xisto** |
| atax | 8 ms | **7 ms** | 18 ms | 0.39 | **0.04 s** | 0.09 s | **32.4 MB** | 61.9 MB | **Xisto** |
| bicg | 13 ms | **7 ms** | 23 ms | 0.30 | **0.04 s** | 0.09 s | **32.5 MB** | 57.3 MB | **Xisto** |
| cholesky | 1.99 s | **1.99 s** | 2.82 s | 0.71 | **7.96 s** | 106.63 s | **63.0 MB** | 90.3 MB | **Xisto** |
| correlation | 7.30 s | **731 ms** | 14.83 s | 0.05 | **0.75 s** | 14.93 s | **25.9 MB** | 53.6 MB | **Xisto** |
| covariance | 7.34 s | **726 ms** | 14.74 s | 0.05 | **0.75 s** | 14.82 s | **25.9 MB** | 51.7 MB | **Xisto** |
| deriche | 462 ms | **167 ms** | 1.10 s | 0.15 | **0.27 s** | 1.39 s | **138.4 MB** | 191.6 MB | **Xisto** |
| doitgen | 740 ms | **91 ms** | 2.03 s | 0.04 | **0.12 s** | 2.13 s | **29.0 MB** | 64.7 MB | **Xisto** |
| durbin | 3 ms | **4 ms** | 13 ms | 0.31 | **0.00 s** | 0.03 s | **1.9 MB** | 24.1 MB | **Xisto** |
| fdtd-2d | 1.58 s | **1.76 s** | 8.05 s | 0.22 | **1.78 s** | 8.28 s | **29.5 MB** | 57.6 MB | **Xisto** |
| floyd-warshall | 18.10 s | **11.23 s** | 37.85 s | 0.30 | **11.28 s** | 37.93 s | **32.0 MB** | 56.7 MB | **Xisto** |
| gemm | 416 ms | **436 ms** | 3.58 s | 0.12 | **0.47 s** | 3.69 s | **29.6 MB** | 58.5 MB | **Xisto** |
| gemver | 31 ms | **10 ms** | 88 ms | 0.11 | **0.04 s** | 0.18 s | **32.6 MB** | 58.0 MB | **Xisto** |
| gesummv | 6 ms | **3 ms** | 10 ms | 0.30 | **0.03 s** | 0.08 s | **27.7 MB** | 55.5 MB | **Xisto** |
| gramschmidt | 6.97 s | **1.41 s** | 35.02 s | 0.04 | **1.44 s** | 35.11 s | **31.4 MB** | 58.3 MB | **Xisto** |
| heat-3d | 2.22 s | **1.83 s** | 7.89 s | 0.23 | **1.86 s** | 8.00 s | **31.3 MB** | 64.0 MB | **Xisto** |
| jacobi-1d | 0 ms | **1 ms** | 6 ms | 0.17 | **0.00 s** | 0.02 s | **1.9 MB** | 22.9 MB | **Xisto** |
| jacobi-2d | 1.41 s | **1.32 s** | 5.03 s | 0.26 | **1.34 s** | 5.11 s | **27.8 MB** | 55.8 MB | **Xisto** |
| lu | 5.68 s | **2.33 s** | 25.47 s | 0.09 | **8.43 s** | 129.09 s | **63.1 MB** | 90.6 MB | **Xisto** |
| ludcmp | 5.27 s | **2.33 s** | 25.10 s | 0.09 | **8.29 s** | 130.77 s | **63.2 MB** | 91.2 MB | **Xisto** |
| mvt | 27 ms | **6 ms** | 75 ms | 0.08 | **0.04 s** | 0.15 s | **32.5 MB** | 56.5 MB | **Xisto** |
| nussinov | 4.32 s | **4.11 s** | 14.26 s | 0.29 | **4.13 s** | 14.32 s | **25.9 MB** | 50.7 MB | **Xisto** |
| seidel-2d | 27.68 s | **4.84 s** | 31.51 s | 0.15 | **4.87 s** | 31.56 s | **32.5 MB** | 55.4 MB | **Xisto** |
| symm | 2.07 s | **1.21 s** | 11.10 s | 0.11 | **1.23 s** | 11.18 s | **28.1 MB** | 53.6 MB | **Xisto** |
| syr2k | 4.61 s | **1.13 s** | 13.71 s | 0.08 | **1.16 s** | 13.80 s | **31.4 MB** | 57.1 MB | **Xisto** |
| syrk | 652 ms | **541 ms** | 3.53 s | 0.15 | **0.56 s** | 3.60 s | **22.1 MB** | 47.3 MB | **Xisto** |
| trisolv | 4 ms | **3 ms** | 8 ms | 0.38 | **0.03 s** | 0.06 s | **32.5 MB** | 55.4 MB | **Xisto** |
| trmm | 1.88 s | **266 ms** | 6.27 s | 0.04 | **0.28 s** | 6.33 s | **18.7 MB** | 43.4 MB | **Xisto** |

Source: `bench/results/polybench-2026-11-15-js/polybench-js-ubuntu-xeon-2026-11-15-c53e83d.txt`.

## Graviton2 (Neoverse N1, arm64, 2 vCPUs)

Kernel time: Xisto **30 wins**, 0 ties, 0 losses.

<SpeedupBars page="pb-vs-bun" section="Graviton2" unit="kernel" />

<MemoryBars page="pb-vs-bun" section="Graviton2" unit="kernel" />

| kernel | C (ref.) kernel | Xisto kernel | Bun kernel | ratio | Xisto CPU | Bun CPU | Xisto memory | Bun memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 6.16 s | **1.78 s** | 38.17 s | 0.05 | **1.81 s** | 38.27 s | **37.0 MB** | 74.8 MB | **Xisto** |
| 3mm | 9.94 s | **2.80 s** | 43.68 s | 0.06 | **2.84 s** | 43.90 s | **53.0 MB** | 91.6 MB | **Xisto** |
| adi | 12.05 s | **10.27 s** | 38.17 s | 0.27 | **10.29 s** | 38.42 s | **32.5 MB** | 68.7 MB | **Xisto** |
| atax | 6 ms | **6 ms** | 21 ms | 0.29 | **0.03 s** | 0.10 s | **31.9 MB** | 66.4 MB | **Xisto** |
| bicg | 14 ms | **4 ms** | 20 ms | 0.20 | **0.03 s** | 0.10 s | **32.0 MB** | 69.1 MB | **Xisto** |
| cholesky | 1.96 s | **1.58 s** | 3.20 s | 0.49 | **13.72 s** | 164.97 s | **62.6 MB** | 112.1 MB | **Xisto** |
| correlation | 7.32 s | **2.13 s** | 19.28 s | 0.11 | **2.15 s** | 19.39 s | **25.4 MB** | 63.1 MB | **Xisto** |
| covariance | 7.36 s | **2.01 s** | 20.01 s | 0.10 | **2.02 s** | 20.10 s | **25.4 MB** | 61.2 MB | **Xisto** |
| deriche | 330 ms | **197 ms** | 1.74 s | 0.11 | **0.28 s** | 1.99 s | **139.3 MB** | 191.4 MB | **Xisto** |
| doitgen | 631 ms | **207 ms** | 3.61 s | 0.06 | **0.23 s** | 3.70 s | **29.3 MB** | 64.0 MB | **Xisto** |
| durbin | 3 ms | **3 ms** | 19 ms | 0.16 | **0.00 s** | 0.04 s | **1.3 MB** | 23.6 MB | **Xisto** |
| fdtd-2d | 1.93 s | **1.92 s** | 12.76 s | 0.15 | **1.95 s** | 13.05 s | **29.2 MB** | 68.3 MB | **Xisto** |
| floyd-warshall | 23.16 s | **14.00 s** | 52.25 s | 0.27 | **14.09 s** | 52.31 s | **31.6 MB** | 56.3 MB | **Xisto** |
| gemm | 679 ms | **647 ms** | 5.11 s | 0.13 | **0.67 s** | 5.24 s | **29.3 MB** | 61.8 MB | **Xisto** |
| gemver | 39 ms | **20 ms** | 117 ms | 0.17 | **0.05 s** | 0.22 s | **32.0 MB** | 69.9 MB | **Xisto** |
| gesummv | 6 ms | **2 ms** | 17 ms | 0.12 | **0.02 s** | 0.08 s | **27.1 MB** | 59.8 MB | **Xisto** |
| gramschmidt | 13.42 s | **2.68 s** | 35.68 s | 0.08 | **2.70 s** | 35.76 s | **30.9 MB** | 69.6 MB | **Xisto** |
| heat-3d | 3.50 s | **3.88 s** | 10.74 s | 0.36 | **3.90 s** | 10.86 s | **30.7 MB** | 63.1 MB | **Xisto** |
| jacobi-1d | 1 ms | **1 ms** | 8 ms | 0.12 | **0.00 s** | 0.02 s | **1.3 MB** | 22.7 MB | **Xisto** |
| jacobi-2d | 1.98 s | **1.96 s** | 6.71 s | 0.29 | **1.98 s** | 6.76 s | **27.3 MB** | 59.7 MB | **Xisto** |
| lu | 12.06 s | **5.40 s** | 45.12 s | 0.12 | **17.64 s** | 209.06 s | **62.7 MB** | 111.8 MB | **Xisto** |
| ludcmp | 10.35 s | **5.03 s** | 41.40 s | 0.12 | **17.06 s** | 233.29 s | **62.6 MB** | 112.5 MB | **Xisto** |
| mvt | 35 ms | **13 ms** | 111 ms | 0.12 | **0.04 s** | 0.20 s | **32.0 MB** | 68.8 MB | **Xisto** |
| nussinov | 9.93 s | **7.59 s** | 39.36 s | 0.19 | **7.60 s** | 39.40 s | **25.4 MB** | 50.1 MB | **Xisto** |
| seidel-2d | 20.60 s | **6.81 s** | 25.11 s | 0.27 | **6.84 s** | 25.17 s | **32.0 MB** | 67.2 MB | **Xisto** |
| symm | 4.55 s | **4.33 s** | 17.33 s | 0.25 | **4.35 s** | 17.41 s | **27.8 MB** | 61.9 MB | **Xisto** |
| syr2k | 5.85 s | **2.90 s** | 22.84 s | 0.13 | **2.92 s** | 22.94 s | **31.1 MB** | 64.5 MB | **Xisto** |
| syrk | 2.11 s | **1.01 s** | 9.24 s | 0.11 | **1.03 s** | 9.31 s | **21.8 MB** | 53.6 MB | **Xisto** |
| trisolv | 3 ms | **2 ms** | 8 ms | 0.25 | **0.02 s** | 0.07 s | **31.9 MB** | 67.7 MB | **Xisto** |
| trmm | 3.85 s | **720 ms** | 13.50 s | 0.05 | **0.73 s** | 13.56 s | **18.2 MB** | 48.6 MB | **Xisto** |

Source: `bench/results/polybench-2026-11-15-js/polybench-js-arm-graviton2-2026-11-15-c5748f6.txt`.
