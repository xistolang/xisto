---
title: Xisto vs Go
sidebar_position: 2
---

import {SpeedupBars, MemoryBars} from '@site/src/components/Charts/Speedup';

# Xisto vs Go

Go has no community PolyBench port, so its side is a line-by-line transliteration of the C ([bench/polybench/go](https://github.com/xistolang/xisto/tree/main/bench/polybench/go)), reviewed against it, with slices of slices (`[][]float64`) like Xisto's `number[][]`, built with Go 1.27. Kernel time is PolyBench's own timer around the kernel, the same region in every port; CPU is the whole process's user + system time; memory is peak RSS. LARGE dataset, median of 3 runs (10 for kernels under 50 ms). Every run's array dump hashes the same as the C reference's. Round of main `40cdc16`, 14 Nov 2026; kernels marked ² were close in the round and re-measured with the four sides interleaved in one invocation (median of 3 × 10 runs): their kernel times are the interleaved ones.

## Xeon E5-2686 v4 (x86-64, 2 vCPUs, AVX2)

Kernel time: Xisto **30 wins**, 0 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

<SpeedupBars page="pb-vs-go" section="Xeon E5-2686 v4" unit="kernel" />

<MemoryBars page="pb-vs-go" section="Xeon E5-2686 v4" unit="kernel" />

| kernel | C (ref.) kernel | Xisto kernel | Go kernel | ratio | Xisto CPU | Go CPU | Xisto memory | Go memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 2.73 s | **620 ms** | 5.84 s | 0.11 | **0.65 s** | 5.90 s | 37.4 MB | 37.8 MB | **Xisto** |
| 3mm | 4.28 s | **1.04 s** | 9.51 s | 0.11 | **1.09 s** | 9.58 s | 53.2 MB | 53.5 MB | **Xisto** |
| adi | 12.44 s | **6.28 s** | 15.72 s | 0.40 | **6.30 s** | 15.79 s | 32.8 MB | 33.0 MB | **Xisto** |
| atax² | 7 ms | **8 ms** | 18 ms | 0.44 | **0.04 s** | 0.05 s | 32.4 MB | 32.7 MB | **Xisto** |
| bicg | 14 ms | **7 ms** | 14 ms | 0.50 | 0.04 s | 0.04 s | 32.5 MB | 32.7 MB | **Xisto** |
| cholesky | 2.00 s | **1.98 s** | 4.43 s | 0.45 | **8.04 s** | 31.45 s | 63.0 MB | 63.4 MB | **Xisto** |
| correlation | 7.19 s | **724 ms** | 6.96 s | 0.10 | **0.75 s** | 6.99 s | 25.8 MB | 26.0 MB | **Xisto** |
| covariance | 7.19 s | **742 ms** | 7.05 s | 0.11 | **0.76 s** | 7.09 s | 25.9 MB | 26.0 MB | **Xisto** |
| deriche | 463 ms | **165 ms** | 470 ms | 0.35 | **0.27 s** | 0.51 s | 138.5 MB | 137.5 MB | **Xisto** |
| doitgen | 740 ms | **97 ms** | 1.66 s | 0.06 | **0.12 s** | 1.69 s | 29.0 MB | **28.0 MB** | **Xisto** |
| durbin² | 4 ms | **4 ms** | 5 ms | 0.80 | **0.00 s** | 0.01 s | **1.9 MB** | 2.0 MB | **Xisto** |
| fdtd-2d² | 1.63 s | **1.62 s** | 3.98 s | 0.41 | **1.68 s** | 4.00 s | 29.5 MB | 29.9 MB | **Xisto** |
| floyd-warshall | 18.10 s | **10.46 s** | 37.13 s | 0.28 | **10.51 s** | 37.32 s | 32.0 MB | 32.0 MB | **Xisto** |
| gemm | 444 ms | **434 ms** | 1.16 s | 0.38 | **0.46 s** | 1.19 s | 29.6 MB | 30.0 MB | **Xisto** |
| gemver | 29 ms | **10 ms** | 44 ms | 0.23 | **0.04 s** | 0.08 s | 32.6 MB | 32.9 MB | **Xisto** |
| gesummv | 6 ms | **3 ms** | 6 ms | 0.50 | 0.03 s | 0.03 s | 27.7 MB | 28.2 MB | **Xisto** |
| gramschmidt | 7.21 s | **1.40 s** | 6.37 s | 0.22 | **1.43 s** | 6.42 s | 31.4 MB | 31.7 MB | **Xisto** |
| heat-3d | 1.88 s | **1.79 s** | 6.75 s | 0.27 | **1.81 s** | 6.80 s | 31.2 MB | **28.7 MB** | **Xisto** |
| jacobi-1d | 1 ms | **1 ms** | 3 ms | 0.33 | 0.00 s | 0.00 s | **1.8 MB** | 2.0 MB | **Xisto** |
| jacobi-2d | 1.30 s | **1.24 s** | 3.91 s | 0.32 | **1.27 s** | 3.95 s | 27.9 MB | 28.2 MB | **Xisto** |
| lu | 5.50 s | **2.10 s** | 10.31 s | 0.20 | **8.11 s** | 38.28 s | 63.0 MB | 63.4 MB | **Xisto** |
| ludcmp | 5.24 s | **2.23 s** | 5.91 s | 0.38 | **8.24 s** | 32.68 s | 63.0 MB | 63.5 MB | **Xisto** |
| mvt | 25 ms | **6 ms** | 35 ms | 0.17 | **0.04 s** | 0.07 s | 32.5 MB | 32.8 MB | **Xisto** |
| nussinov | 4.13 s | **4.08 s** | 9.26 s | 0.44 | **4.10 s** | 9.31 s | 26.0 MB | 26.0 MB | **Xisto** |
| seidel-2d | 27.68 s | **4.85 s** | 31.38 s | 0.15 | **4.87 s** | 31.51 s | 32.5 MB | 32.7 MB | **Xisto** |
| symm | 1.98 s | **1.12 s** | 2.74 s | 0.41 | **1.14 s** | 2.77 s | 28.0 MB | 28.3 MB | **Xisto** |
| syr2k | 4.24 s | **1.07 s** | 4.26 s | 0.25 | **1.10 s** | 4.31 s | 31.4 MB | 31.7 MB | **Xisto** |
| syrk | 648 ms | **553 ms** | 1.06 s | 0.52 | **0.57 s** | 1.09 s | 22.1 MB | 22.4 MB | **Xisto** |
| trisolv | 3 ms | **4 ms** | 7 ms | 0.57 | 0.03 s | 0.03 s | 32.5 MB | **24.2 MB** | **Xisto** |
| trmm | 1.79 s | **275 ms** | 2.34 s | 0.12 | **0.29 s** | 2.36 s | 18.7 MB | 18.3 MB | **Xisto** |

## Graviton2 (Neoverse N1, arm64, 2 vCPUs)

Kernel time: Xisto **30 wins**, 0 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

<SpeedupBars page="pb-vs-go" section="Graviton2" unit="kernel" />

<MemoryBars page="pb-vs-go" section="Graviton2" unit="kernel" />

| kernel | C (ref.) kernel | Xisto kernel | Go kernel | ratio | Xisto CPU | Go CPU | Xisto memory | Go memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 6.21 s | **1.68 s** | 9.58 s | 0.17 | **1.71 s** | 9.59 s | 37.1 MB | 37.6 MB | **Xisto** |
| 3mm | 10.11 s | **2.97 s** | 17.44 s | 0.17 | **3.02 s** | 17.55 s | 53.0 MB | 53.5 MB | **Xisto** |
| adi | 12.12 s | **10.35 s** | 15.64 s | 0.66 | **10.37 s** | 15.72 s | 32.5 MB | 32.8 MB | **Xisto** |
| atax | 6 ms | **6 ms** | 19 ms | 0.32 | **0.03 s** | 0.05 s | 31.9 MB | 32.6 MB | **Xisto** |
| bicg | 14 ms | **4 ms** | 15 ms | 0.27 | **0.03 s** | 0.05 s | 32.1 MB | 32.6 MB | **Xisto** |
| cholesky | 1.88 s | **1.58 s** | 4.47 s | 0.35 | **13.88 s** | 43.23 s | 62.7 MB | 63.3 MB | **Xisto** |
| correlation | 7.55 s | **2.06 s** | 7.75 s | 0.27 | **2.08 s** | 7.79 s | 25.4 MB | 25.9 MB | **Xisto** |
| covariance | 7.43 s | **2.05 s** | 7.75 s | 0.26 | **2.06 s** | 7.79 s | 25.4 MB | 25.9 MB | **Xisto** |
| deriche | 330 ms | **195 ms** | 364 ms | 0.54 | **0.27 s** | 0.42 s | 139.4 MB | 137.5 MB | **Xisto** |
| doitgen | 657 ms | **209 ms** | 1.61 s | 0.13 | **0.23 s** | 1.64 s | 29.2 MB | **27.9 MB** | **Xisto** |
| durbin | 3 ms | **3 ms** | 6 ms | 0.50 | **0.00 s** | 0.01 s | **1.3 MB** | 1.9 MB | **Xisto** |
| fdtd-2d | 1.78 s | **1.90 s** | 4.76 s | 0.40 | **1.93 s** | 4.81 s | 29.1 MB | 29.7 MB | **Xisto** |
| floyd-warshall | 23.08 s | **14.01 s** | 60.72 s | 0.23 | **14.09 s** | 61.06 s | 31.6 MB | 31.9 MB | **Xisto** |
| gemm² | 642 ms | **633 ms** | 4.83 s | 0.13 | **0.67 s** | 4.86 s | 29.3 MB | 29.8 MB | **Xisto** |
| gemver | 39 ms | **20 ms** | 51 ms | 0.39 | **0.05 s** | 0.08 s | 32.1 MB | 32.9 MB | **Xisto** |
| gesummv | 6 ms | **2 ms** | 6 ms | 0.33 | **0.02 s** | 0.03 s | 27.3 MB | 28.1 MB | **Xisto** |
| gramschmidt | 13.25 s | **2.61 s** | 13.80 s | 0.19 | **2.63 s** | 13.89 s | 30.9 MB | 31.4 MB | **Xisto** |
| heat-3d | 3.50 s | **3.66 s** | 8.84 s | 0.41 | **3.69 s** | 8.90 s | 30.7 MB | **28.6 MB** | **Xisto** |
| jacobi-1d | 1 ms | **1 ms** | 4 ms | 0.25 | **0.00 s** | 0.01 s | **1.3 MB** | 1.8 MB | **Xisto** |
| jacobi-2d | 1.96 s | **1.96 s** | 5.89 s | 0.33 | **1.98 s** | 5.94 s | 27.4 MB | 27.9 MB | **Xisto** |
| lu | 12.41 s | **5.22 s** | 14.10 s | 0.37 | **17.56 s** | 53.19 s | 62.7 MB | 63.3 MB | **Xisto** |
| ludcmp | 10.48 s | **5.16 s** | 8.76 s | 0.59 | **17.78 s** | 47.53 s | 62.6 MB | 63.3 MB | **Xisto** |
| mvt | 35 ms | **13 ms** | 38 ms | 0.34 | **0.04 s** | 0.07 s | 32.1 MB | 32.7 MB | **Xisto** |
| nussinov | 9.91 s | **7.72 s** | 17.80 s | 0.43 | **7.73 s** | 17.89 s | 25.5 MB | 25.9 MB | **Xisto** |
| seidel-2d | 20.61 s | **6.86 s** | 24.45 s | 0.28 | **6.89 s** | 24.57 s | 32.0 MB | 32.6 MB | **Xisto** |
| symm | 4.58 s | **4.25 s** | 5.31 s | 0.80 | **4.27 s** | 5.36 s | 27.8 MB | 28.2 MB | **Xisto** |
| syr2k | 5.98 s | **2.74 s** | 6.71 s | 0.41 | **2.77 s** | 6.74 s | 31.1 MB | 31.6 MB | **Xisto** |
| syrk | 2.02 s | **1.06 s** | 3.25 s | 0.33 | **1.08 s** | 3.28 s | 21.7 MB | 22.2 MB | **Xisto** |
| trisolv | 3 ms | **3 ms** | 7 ms | 0.43 | 0.02 s | 0.02 s | 32.0 MB | **24.1 MB** | **Xisto** |
| trmm | 3.90 s | **650 ms** | 4.43 s | 0.15 | **0.66 s** | 4.46 s | 18.2 MB | 18.1 MB | **Xisto** |

## Notes

- Go's compiler does not vectorize nor unroll-and-jam loops, and it keeps bounds checks it cannot prove away, which is most of the distance on the matrix kernels.
- Go is not ahead of Xisto on any kernel here.

Raw output: [bench/results/polybench-2026-11-14](https://github.com/xistolang/xisto/tree/main/bench/results/polybench-2026-11-14).
