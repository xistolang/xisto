---
title: Xisto vs Rust
sidebar_position: 1
---

# Xisto vs Rust

Rust's side is the community port [polybench-rs](https://github.com/JRF63/polybench-rs): every kernel with fixed-size contiguous arrays (`Box<[[f64; N]; M]>`), release build with `-C target-cpu=native`. Xisto's side is the TypeScript port with `number[][]`, the same loops in the same order. Kernel time is PolyBench's own timer around the kernel, the same region in every port; CPU is the whole process's user + system time; memory is peak RSS. LARGE dataset, median of 3 runs (10 for kernels under 50 ms). Every run's array dump hashes the same as the C reference's. Round of main `40cdc16`, 14 Nov 2026; kernels marked ² were close in the round and re-measured with the four sides interleaved in one invocation (median of 3 × 10 runs): their kernel times are the interleaved ones.

## Xeon E5-2686 v4 (x86-64, 2 vCPUs, AVX2)

Kernel time: Xisto **21 wins**, 9 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

| kernel | C (ref.) kernel | Xisto kernel | Rust kernel | ratio | Xisto CPU | Rust CPU | Xisto memory | Rust memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 2.73 s | **620 ms** | 2.67 s | 0.23 | **0.65 s** | 2.70 s | 37.4 MB | 37.4 MB | **Xisto** |
| 3mm | 4.28 s | **1.04 s** | 4.07 s | 0.26 | **1.09 s** | 4.11 s | 53.2 MB | 53.2 MB | **Xisto** |
| adi | 12.44 s | **6.28 s** | 14.03 s | 0.45 | **6.30 s** | 14.05 s | 32.8 MB | 32.7 MB | **Xisto** |
| atax² | 7 ms | 8 ms | 8 ms | 1.00 | 0.04 s | 0.04 s | 32.4 MB | 32.6 MB | tie |
| bicg | 14 ms | 7 ms | 7 ms | 1.00 | 0.04 s | 0.04 s | 32.5 MB | 32.7 MB | tie |
| cholesky | 2.00 s | 1.98 s | 2.00 s | 0.99 | **8.04 s** | 32.19 s | 63.0 MB | 63.1 MB | tie |
| correlation | 7.19 s | **724 ms** | 7.26 s | 0.10 | **0.75 s** | 7.28 s | 25.8 MB | 26.0 MB | **Xisto** |
| covariance | 7.19 s | **742 ms** | 7.31 s | 0.10 | **0.76 s** | 7.33 s | 25.9 MB | 25.9 MB | **Xisto** |
| deriche | 463 ms | **165 ms** | 401 ms | 0.41 | **0.27 s** | 0.51 s | 138.5 MB | 137.2 MB | **Xisto** |
| doitgen | 740 ms | **97 ms** | 524 ms | 0.19 | **0.12 s** | 0.55 s | 29.0 MB | **28.0 MB** | **Xisto** |
| durbin² | 4 ms | 4 ms | 4 ms | 1.00 | 0.00 s | 0.00 s | **1.9 MB** | 2.3 MB | tie |
| fdtd-2d² | 1.63 s | 1.62 s | 1.64 s | 0.98 | 1.68 s | **1.56 s** | 29.5 MB | 29.6 MB | tie |
| floyd-warshall | 18.10 s | **10.46 s** | 18.75 s | 0.56 | **10.51 s** | 18.79 s | 32.0 MB | 32.1 MB | **Xisto** |
| gemm | 444 ms | 434 ms | 422 ms | 1.03 | 0.46 s | 0.45 s | 29.6 MB | 29.7 MB | tie |
| gemver | 29 ms | **10 ms** | 29 ms | 0.34 | **0.04 s** | 0.06 s | 32.6 MB | 32.8 MB | **Xisto** |
| gesummv | 6 ms | 3 ms | 3 ms | 1.00 | 0.03 s | 0.03 s | 27.7 MB | 28.0 MB | tie |
| gramschmidt | 7.21 s | **1.40 s** | 7.35 s | 0.19 | **1.43 s** | 7.37 s | 31.4 MB | 31.5 MB | **Xisto** |
| heat-3d | 1.88 s | **1.79 s** | 2.00 s | 0.89 | **1.81 s** | 2.03 s | 31.2 MB | **28.5 MB** | **Xisto** |
| jacobi-1d | 1 ms | 1 ms | 1 ms | 1.00 | 0.00 s | 0.00 s | **1.8 MB** | 2.2 MB | tie |
| jacobi-2d | 1.30 s | **1.24 s** | 1.41 s | 0.88 | **1.27 s** | 1.43 s | 27.9 MB | 28.0 MB | **Xisto** |
| lu | 5.50 s | **2.10 s** | 5.57 s | 0.38 | **8.11 s** | 33.33 s | 63.0 MB | 63.1 MB | **Xisto** |
| ludcmp | 5.24 s | **2.23 s** | 5.46 s | 0.41 | **8.24 s** | 33.17 s | 63.0 MB | 63.1 MB | **Xisto** |
| mvt | 25 ms | **6 ms** | 25 ms | 0.24 | **0.04 s** | 0.06 s | 32.5 MB | 32.7 MB | **Xisto** |
| nussinov | 4.13 s | **4.08 s** | 4.58 s | 0.89 | **4.10 s** | 4.60 s | 26.0 MB | 26.1 MB | **Xisto** |
| seidel-2d | 27.68 s | **4.85 s** | 27.67 s | 0.18 | **4.87 s** | 27.69 s | 32.5 MB | 32.7 MB | **Xisto** |
| symm | 1.98 s | **1.12 s** | 2.21 s | 0.51 | **1.14 s** | 2.23 s | 28.0 MB | 28.1 MB | **Xisto** |
| syr2k | 4.24 s | **1.07 s** | 4.04 s | 0.27 | **1.10 s** | 4.06 s | 31.4 MB | 31.5 MB | **Xisto** |
| syrk | 648 ms | **553 ms** | 642 ms | 0.86 | **0.57 s** | 0.66 s | 22.1 MB | 22.3 MB | **Xisto** |
| trisolv | 3 ms | 4 ms | 4 ms | 1.00 | 0.03 s | 0.03 s | 32.5 MB | 32.7 MB | tie |
| trmm | 1.79 s | **275 ms** | 1.80 s | 0.15 | **0.29 s** | 1.81 s | 18.7 MB | 19.0 MB | **Xisto** |

## Graviton2 (Neoverse N1, arm64, 2 vCPUs)

Kernel time: Xisto **21 wins**, 9 ties, 0 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

| kernel | C (ref.) kernel | Xisto kernel | Rust kernel | ratio | Xisto CPU | Rust CPU | Xisto memory | Rust memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 6.21 s | **1.68 s** | 5.32 s | 0.31 | **1.71 s** | 5.35 s | 37.1 MB | 37.1 MB | **Xisto** |
| 3mm | 10.11 s | **2.97 s** | 7.27 s | 0.41 | **3.02 s** | 7.30 s | 53.0 MB | 52.9 MB | **Xisto** |
| adi | 12.12 s | **10.35 s** | 11.42 s | 0.91 | **10.37 s** | 11.43 s | 32.5 MB | 32.4 MB | **Xisto** |
| atax | 6 ms | 6 ms | 6 ms | 1.00 | 0.03 s | 0.03 s | 31.9 MB | 32.2 MB | tie |
| bicg | 14 ms | 4 ms | 4 ms | 1.00 | 0.03 s | 0.03 s | 32.1 MB | 32.2 MB | tie |
| cholesky | 1.88 s | **1.58 s** | 1.97 s | 0.80 | **13.88 s** | 45.50 s | 62.7 MB | 62.8 MB | **Xisto** |
| correlation | 7.55 s | **2.06 s** | 7.11 s | 0.29 | **2.08 s** | 7.12 s | 25.4 MB | 25.5 MB | **Xisto** |
| covariance | 7.43 s | **2.05 s** | 7.13 s | 0.29 | **2.06 s** | 7.14 s | 25.4 MB | 25.6 MB | **Xisto** |
| deriche | 330 ms | **195 ms** | 287 ms | 0.68 | **0.27 s** | 0.35 s | 139.4 MB | 136.8 MB | **Xisto** |
| doitgen | 657 ms | **209 ms** | 523 ms | 0.40 | **0.23 s** | 0.54 s | 29.2 MB | **27.7 MB** | **Xisto** |
| durbin | 3 ms | 3 ms | 3 ms | 1.00 | 0.00 s | 0.00 s | **1.3 MB** | 1.9 MB | tie |
| fdtd-2d | 1.78 s | 1.90 s | 1.94 s | 0.98 | 1.93 s | 1.96 s | 29.1 MB | 29.2 MB | tie |
| floyd-warshall | 23.08 s | **14.01 s** | 23.73 s | 0.59 | **14.09 s** | 23.78 s | 31.6 MB | 31.7 MB | **Xisto** |
| gemm² | 642 ms | 633 ms | 630 ms | 1.00 | 0.67 s | **0.64 s** | 29.3 MB | 29.3 MB | tie |
| gemver | 39 ms | **20 ms** | 37 ms | 0.54 | **0.05 s** | 0.06 s | 32.1 MB | 32.5 MB | **Xisto** |
| gesummv | 6 ms | 2 ms | 2 ms | 1.00 | 0.02 s | 0.02 s | 27.3 MB | 27.6 MB | tie |
| gramschmidt | 13.25 s | **2.61 s** | 15.57 s | 0.17 | **2.63 s** | 15.59 s | 30.9 MB | 31.1 MB | **Xisto** |
| heat-3d | 3.50 s | **3.66 s** | 3.98 s | 0.92 | **3.69 s** | 3.99 s | 30.7 MB | **28.0 MB** | **Xisto** |
| jacobi-1d | 1 ms | 1 ms | 1 ms | 1.00 | 0.00 s | 0.00 s | **1.3 MB** | 1.9 MB | tie |
| jacobi-2d | 1.96 s | 1.96 s | 1.92 s | 1.02 | 1.98 s | 1.93 s | 27.4 MB | 27.6 MB | tie |
| lu | 12.41 s | **5.22 s** | 13.90 s | 0.38 | **17.56 s** | 58.09 s | 62.7 MB | 62.8 MB | **Xisto** |
| ludcmp | 10.48 s | **5.16 s** | 10.65 s | 0.48 | **17.78 s** | 54.49 s | 62.6 MB | 62.9 MB | **Xisto** |
| mvt | 35 ms | **13 ms** | 24 ms | 0.54 | **0.04 s** | 0.05 s | 32.1 MB | 32.4 MB | **Xisto** |
| nussinov | 9.91 s | **7.72 s** | 10.38 s | 0.74 | **7.73 s** | 10.40 s | 25.5 MB | 25.6 MB | **Xisto** |
| seidel-2d | 20.61 s | **6.86 s** | 20.55 s | 0.33 | **6.89 s** | 20.56 s | 32.0 MB | 32.3 MB | **Xisto** |
| symm | 4.58 s | **4.25 s** | 4.52 s | 0.94 | **4.27 s** | 4.54 s | 27.8 MB | 27.8 MB | **Xisto** |
| syr2k | 5.98 s | **2.74 s** | 6.33 s | 0.43 | **2.77 s** | 6.35 s | 31.1 MB | 31.1 MB | **Xisto** |
| syrk | 2.02 s | **1.06 s** | 1.90 s | 0.56 | **1.08 s** | 1.92 s | 21.7 MB | 22.0 MB | **Xisto** |
| trisolv | 3 ms | 3 ms | 3 ms | 1.00 | 0.02 s | 0.02 s | 32.0 MB | 32.3 MB | tie |
| trmm | 3.90 s | **650 ms** | 3.95 s | 0.16 | **0.66 s** | 3.96 s | 18.2 MB | 18.6 MB | **Xisto** |

## Notes

- **atax, durbin, gesummv, jacobi-1d, trisolv** run in a few milliseconds; at the timer's millisecond resolution a difference of 1 ms is 15–30%, so they were re-measured interleaved where the round put them apart.
- **jacobi-2d on the Xeon** now runs its two sweeps fused one row apart (as heat-3d), and **symm on the Graviton2** allocates its rows on 64-byte lines and jams two columns instead of sixteen; both were behind Rust in the round before (main `02c991f`: jacobi-2d 1.45 s against 1.40 s, symm 5.05 s against 4.52 s).
- Rust's arrays are one block per matrix; Xisto's `number[][]` rows are separate allocations, as in any TypeScript program. The kernels where Xisto wins by 2–10× (2mm, 3mm, correlation, covariance, seidel-2d...) are loop transformations of the compiler, not a different algorithm: the same operations in the same order per element.

Raw output: [bench/results/polybench-2026-11-14](https://github.com/xistolang/xisto/tree/main/bench/results/polybench-2026-11-14).
