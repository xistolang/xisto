---
title: Xisto vs Go
sidebar_position: 2
---

# Xisto vs Go

Go has no community PolyBench port, so its side is a line-by-line transliteration of the C ([bench/polybench/go](https://github.com/xistolang/xisto/tree/main/bench/polybench/go)), reviewed against it, with slices of slices (`[][]float64`) like Xisto's `number[][]`, built with Go 1.27. Kernel time is PolyBench's own timer around the kernel, the same region in every port; CPU is the whole process's user + system time; memory is peak RSS. LARGE dataset, median of 3 runs (10 for kernels under 50 ms). Every run's array dump hashes the same as the C reference's. Round of main `9a6c2d3`, 14 Nov 2026.

## Xeon E5-2686 v4 (x86-64, 2 vCPUs, AVX2)

Kernel time: Xisto **29 wins**, 1 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

| kernel | C (ref.) kernel | Xisto kernel | Go kernel | ratio | Xisto CPU | Go CPU | Xisto memory | Go memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 2.75 s | **623 ms** | 5.78 s | 0.11 | **0.66 s** | 5.84 s | 37.4 MB | 37.8 MB | **Xisto** |
| 3mm | 4.26 s | **1.06 s** | 9.54 s | 0.11 | **1.11 s** | 9.61 s | 53.3 MB | 53.7 MB | **Xisto** |
| adi | 12.62 s | **6.30 s** | 15.79 s | 0.40 | **6.33 s** | 15.86 s | 32.8 MB | 33.0 MB | **Xisto** |
| atax | 8 ms | **8 ms** | 18 ms | 0.44 | **0.04 s** | 0.05 s | 32.4 MB | 32.7 MB | **Xisto** |
| bicg | 14 ms | **7 ms** | 14 ms | 0.50 | 0.04 s | 0.04 s | 32.5 MB | 32.8 MB | **Xisto** |
| cholesky | 2.00 s | **1.98 s** | 4.47 s | 0.44 | **7.92 s** | 31.74 s | 63.0 MB | 63.4 MB | **Xisto** |
| correlation | 7.28 s | **728 ms** | 7.16 s | 0.10 | **0.75 s** | 7.20 s | 25.9 MB | 26.0 MB | **Xisto** |
| covariance | 7.28 s | **724 ms** | 7.12 s | 0.10 | **0.74 s** | 7.16 s | 25.9 MB | 25.9 MB | **Xisto** |
| deriche | 464 ms | **168 ms** | 469 ms | 0.36 | **0.27 s** | 0.51 s | 138.5 MB | 137.6 MB | **Xisto** |
| doitgen | 739 ms | **91 ms** | 1.66 s | 0.05 | **0.12 s** | 1.70 s | 28.9 MB | **27.9 MB** | **Xisto** |
| durbin | 4 ms | 5 ms | 5 ms | 1.00 | 0.01 s | 0.01 s | **1.9 MB** | 2.0 MB | tie |
| fdtd-2d | 1.60 s | **1.61 s** | 4.08 s | 0.39 | **1.63 s** | 4.12 s | 29.5 MB | 29.9 MB | **Xisto** |
| floyd-warshall | 18.39 s | **18.83 s** | 37.21 s | 0.51 | **18.88 s** | 37.39 s | 32.0 MB | 32.0 MB | **Xisto** |
| gemm | 421 ms | **419 ms** | 1.16 s | 0.36 | **0.45 s** | 1.19 s | 29.6 MB | 29.9 MB | **Xisto** |
| gemver | 30 ms | **11 ms** | 44 ms | 0.25 | **0.04 s** | 0.08 s | 32.6 MB | 32.9 MB | **Xisto** |
| gesummv | 6 ms | **3 ms** | 6 ms | 0.50 | 0.03 s | 0.03 s | 27.7 MB | 28.2 MB | **Xisto** |
| gramschmidt | 7.71 s | **1.40 s** | 6.82 s | 0.20 | **1.42 s** | 6.87 s | 31.4 MB | 31.7 MB | **Xisto** |
| heat-3d | 2.12 s | **2.17 s** | 6.78 s | 0.32 | **2.19 s** | 6.83 s | 31.3 MB | **28.8 MB** | **Xisto** |
| jacobi-1d | 0 ms | **1 ms** | 3 ms | 0.33 | 0.00 s | 0.00 s | **1.8 MB** | 2.0 MB | **Xisto** |
| jacobi-2d | 1.42 s | **1.51 s** | 3.97 s | 0.38 | **1.53 s** | 4.01 s | 27.7 MB | 28.2 MB | **Xisto** |
| lu | 6.17 s | **2.27 s** | 10.53 s | 0.22 | **8.30 s** | 37.44 s | 63.0 MB | 63.3 MB | **Xisto** |
| ludcmp | 5.74 s | **2.41 s** | 5.57 s | 0.43 | **8.50 s** | 32.31 s | 63.0 MB | 63.4 MB | **Xisto** |
| mvt | 27 ms | **7 ms** | 35 ms | 0.20 | **0.04 s** | 0.07 s | 32.5 MB | 32.8 MB | **Xisto** |
| nussinov | 4.17 s | **3.96 s** | 9.24 s | 0.43 | **3.97 s** | 9.28 s | 26.0 MB | 26.0 MB | **Xisto** |
| seidel-2d | 27.68 s | **4.86 s** | 31.38 s | 0.15 | **4.89 s** | 31.51 s | 32.5 MB | 32.7 MB | **Xisto** |
| symm | 2.01 s | **1.12 s** | 2.84 s | 0.39 | **1.14 s** | 2.88 s | 28.0 MB | 28.3 MB | **Xisto** |
| syr2k | 4.72 s | **1.07 s** | 4.62 s | 0.23 | **1.11 s** | 4.67 s | 31.4 MB | 31.7 MB | **Xisto** |
| syrk | 666 ms | **558 ms** | 1.06 s | 0.53 | **0.58 s** | 1.09 s | 22.1 MB | 22.3 MB | **Xisto** |
| trisolv | 4 ms | **3 ms** | 7 ms | 0.43 | 0.03 s | 0.03 s | 32.5 MB | **24.2 MB** | **Xisto** |
| trmm | 1.80 s | **268 ms** | 2.29 s | 0.12 | **0.29 s** | 2.32 s | 18.7 MB | 18.2 MB | **Xisto** |

## Graviton2 (Neoverse N1, arm64, 2 vCPUs)

Kernel time: Xisto **30 wins**, 0 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

| kernel | C (ref.) kernel | Xisto kernel | Go kernel | ratio | Xisto CPU | Go CPU | Xisto memory | Go memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 6.14 s | **1.81 s** | 9.49 s | 0.19 | **1.84 s** | 9.56 s | 37.0 MB | 37.7 MB | **Xisto** |
| 3mm | 10.07 s | **3.18 s** | 17.39 s | 0.18 | **3.22 s** | 17.50 s | 53.0 MB | 53.3 MB | **Xisto** |
| adi | 11.99 s | **10.12 s** | 15.76 s | 0.64 | **10.12 s** | 15.84 s | 32.5 MB | 32.8 MB | **Xisto** |
| atax | 6 ms | **6 ms** | 19 ms | 0.32 | **0.03 s** | 0.05 s | 31.9 MB | 32.6 MB | **Xisto** |
| bicg | 14 ms | **4 ms** | 15 ms | 0.27 | **0.03 s** | 0.05 s | 32.0 MB | 32.6 MB | **Xisto** |
| cholesky | 1.90 s | **1.60 s** | 4.47 s | 0.36 | **13.89 s** | 42.79 s | 62.7 MB | 63.3 MB | **Xisto** |
| correlation | 7.44 s | **2.10 s** | 7.79 s | 0.27 | **2.11 s** | 7.84 s | 25.4 MB | 25.9 MB | **Xisto** |
| covariance | 7.49 s | **2.07 s** | 7.85 s | 0.26 | **2.09 s** | 7.90 s | 25.4 MB | 25.9 MB | **Xisto** |
| deriche | 321 ms | **203 ms** | 376 ms | 0.54 | **0.28 s** | 0.43 s | 139.4 MB | 137.5 MB | **Xisto** |
| doitgen | 628 ms | **209 ms** | 1.61 s | 0.13 | **0.23 s** | 1.65 s | 29.3 MB | **27.9 MB** | **Xisto** |
| durbin | 3 ms | **5 ms** | 6 ms | 0.83 | 0.01 s | 0.01 s | **1.3 MB** | 1.9 MB | **Xisto** |
| fdtd-2d | 1.88 s | **1.88 s** | 4.78 s | 0.39 | **1.90 s** | 4.83 s | 29.3 MB | 29.7 MB | **Xisto** |
| floyd-warshall | 23.11 s | **25.02 s** | 60.85 s | 0.41 | **25.10 s** | 61.19 s | 31.7 MB | 32.1 MB | **Xisto** |
| gemm | 618 ms | **657 ms** | 4.81 s | 0.14 | **0.69 s** | 4.86 s | 29.3 MB | 29.8 MB | **Xisto** |
| gemver | 39 ms | **19 ms** | 51 ms | 0.37 | **0.05 s** | 0.08 s | 32.1 MB | 32.8 MB | **Xisto** |
| gesummv | 6 ms | **2 ms** | 6 ms | 0.33 | **0.02 s** | 0.03 s | 27.3 MB | 28.1 MB | **Xisto** |
| gramschmidt | 13.45 s | **2.62 s** | 13.77 s | 0.19 | **2.64 s** | 13.86 s | 30.9 MB | 31.6 MB | **Xisto** |
| heat-3d | 3.52 s | **3.66 s** | 8.80 s | 0.42 | **3.68 s** | 8.86 s | 30.8 MB | **28.6 MB** | **Xisto** |
| jacobi-1d | 1 ms | **1 ms** | 4 ms | 0.25 | **0.00 s** | 0.01 s | **1.3 MB** | 1.8 MB | **Xisto** |
| jacobi-2d | 1.91 s | **1.92 s** | 5.76 s | 0.33 | **1.94 s** | 5.81 s | 27.4 MB | 27.9 MB | **Xisto** |
| lu | 12.30 s | **5.26 s** | 13.90 s | 0.38 | **17.54 s** | 52.49 s | 62.7 MB | 63.3 MB | **Xisto** |
| ludcmp | 10.40 s | **5.06 s** | 8.64 s | 0.59 | **17.24 s** | 47.51 s | 62.7 MB | 63.3 MB | **Xisto** |
| mvt | 34 ms | **13 ms** | 37 ms | 0.35 | **0.04 s** | 0.07 s | 32.1 MB | 32.7 MB | **Xisto** |
| nussinov | 9.92 s | **7.71 s** | 17.63 s | 0.44 | **7.72 s** | 17.73 s | 25.5 MB | 25.9 MB | **Xisto** |
| seidel-2d | 20.63 s | **6.76 s** | 24.45 s | 0.28 | **6.78 s** | 24.57 s | 32.0 MB | 32.6 MB | **Xisto** |
| symm | 4.59 s | **5.08 s** | 5.40 s | 0.94 | **5.10 s** | 5.43 s | 27.7 MB | 28.2 MB | **Xisto** |
| syr2k | 5.99 s | **2.76 s** | 6.63 s | 0.42 | **2.79 s** | 6.67 s | 31.2 MB | 31.6 MB | **Xisto** |
| syrk | 2.04 s | **1.00 s** | 3.21 s | 0.31 | **1.02 s** | 3.24 s | 21.7 MB | 22.2 MB | **Xisto** |
| trisolv | 3 ms | **3 ms** | 7 ms | 0.43 | 0.02 s | 0.02 s | 32.1 MB | **24.1 MB** | **Xisto** |
| trmm | 3.91 s | **627 ms** | 4.39 s | 0.14 | **0.64 s** | 4.43 s | 18.3 MB | 18.1 MB | **Xisto** |

## Notes

- **floyd-warshall and heat-3d** were improved after this round (`b0c603b`): floyd-warshall 14.1 s on the Graviton2 and 10.6 s on the Xeon, heat-3d 1.83 s on the Xeon; Xisto now wins both on both machines. See [PolyBench](./index.md#the-last-two-fixed-after-the-round-b0c603b).

- Go's compiler does not vectorize nor unroll-and-jam loops, and it keeps bounds checks it cannot prove away, which is most of the distance on the matrix kernels.
- Go is not ahead of Xisto on any kernel here.

Raw output: [bench/results/polybench-2026-11-14](https://github.com/xistolang/xisto/tree/main/bench/results/polybench-2026-11-14).
