---
title: Xisto vs Rust
sidebar_position: 1
---

# Xisto vs Rust

Rust's side is the community port [polybench-rs](https://github.com/JRF63/polybench-rs): every kernel with fixed-size contiguous arrays (`Box<[[f64; N]; M]>`), release build with `-C target-cpu=native`. Xisto's side is the TypeScript port with `number[][]`, the same loops in the same order. Kernel time is PolyBench's own timer around the kernel, the same region in every port; CPU is the whole process's user + system time; memory is peak RSS. LARGE dataset, median of 3 runs (10 for kernels under 50 ms). Every run's array dump hashes the same as the C reference's. Round of main `9a6c2d3`, 14 Nov 2026.

## Xeon E5-2686 v4 (x86-64, 2 vCPUs, AVX2)

Kernel time: Xisto **21 wins**, 7 ties, 2 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

| kernel | C (ref.) kernel | Xisto kernel | Rust kernel | ratio | Xisto CPU | Rust CPU | Xisto memory | Rust memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 2.75 s | **623 ms** | 2.65 s | 0.23 | **0.66 s** | 2.68 s | 37.4 MB | 37.4 MB | **Xisto** |
| 3mm | 4.26 s | **1.06 s** | 4.06 s | 0.26 | **1.11 s** | 4.11 s | 53.3 MB | 53.2 MB | **Xisto** |
| adi | 12.62 s | **6.30 s** | 14.12 s | 0.45 | **6.33 s** | 14.15 s | 32.8 MB | 32.7 MB | **Xisto** |
| atax | 8 ms | 8 ms | 8 ms | 1.00 | 0.04 s | 0.04 s | 32.4 MB | 32.6 MB | tie |
| bicg | 14 ms | 7 ms | 7 ms | 1.00 | 0.04 s | 0.04 s | 32.5 MB | 32.7 MB | tie |
| cholesky | 2.00 s | 1.98 s | 2.00 s | 0.99 | **7.92 s** | 28.58 s | 63.0 MB | 63.1 MB | tie |
| correlation | 7.28 s | **728 ms** | 7.29 s | 0.10 | **0.75 s** | 7.31 s | 25.9 MB | 25.9 MB | **Xisto** |
| covariance | 7.28 s | **724 ms** | 7.33 s | 0.10 | **0.74 s** | 7.35 s | 25.9 MB | 25.9 MB | **Xisto** |
| deriche | 464 ms | **168 ms** | 403 ms | 0.42 | **0.27 s** | 0.51 s | 138.5 MB | 137.2 MB | **Xisto** |
| doitgen | 739 ms | **91 ms** | 526 ms | 0.17 | **0.12 s** | 0.55 s | 28.9 MB | **28.0 MB** | **Xisto** |
| durbin | 4 ms | 5 ms | **4 ms** | 1.25 | 0.01 s | **0.00 s** | **1.9 MB** | 2.3 MB | **Rust** |
| fdtd-2d | 1.60 s | **1.61 s** | 1.82 s | 0.88 | **1.63 s** | 1.85 s | 29.5 MB | 29.6 MB | **Xisto** |
| floyd-warshall | 18.39 s | 18.83 s | 18.93 s | 0.99 | 18.88 s | 18.96 s | 32.0 MB | 32.1 MB | tie |
| gemm | 421 ms | **419 ms** | 436 ms | 0.96 | 0.45 s | 0.46 s | 29.6 MB | 29.7 MB | **Xisto** |
| gemver | 30 ms | **11 ms** | 29 ms | 0.38 | **0.04 s** | 0.06 s | 32.6 MB | 32.9 MB | **Xisto** |
| gesummv | 6 ms | 3 ms | 3 ms | 1.00 | 0.03 s | 0.03 s | 27.7 MB | 28.0 MB | tie |
| gramschmidt | 7.71 s | **1.40 s** | 6.96 s | 0.20 | **1.42 s** | 6.99 s | 31.4 MB | 31.4 MB | **Xisto** |
| heat-3d | 2.12 s | 2.17 s | **2.03 s** | 1.07 | 2.19 s | **2.05 s** | 31.3 MB | **28.6 MB** | **Rust** |
| jacobi-1d | 0 ms | 1 ms | 1 ms | 1.00 | 0.00 s | 0.00 s | **1.8 MB** | 2.2 MB | tie |
| jacobi-2d | 1.42 s | 1.51 s | 1.48 s | 1.02 | 1.53 s | 1.50 s | 27.7 MB | 28.0 MB | tie |
| lu | 6.17 s | **2.27 s** | 5.89 s | 0.39 | **8.30 s** | 33.57 s | 63.0 MB | 63.1 MB | **Xisto** |
| ludcmp | 5.74 s | **2.41 s** | 5.30 s | 0.45 | **8.50 s** | 35.57 s | 63.0 MB | 63.1 MB | **Xisto** |
| mvt | 27 ms | **7 ms** | 26 ms | 0.27 | **0.04 s** | 0.06 s | 32.5 MB | 32.7 MB | **Xisto** |
| nussinov | 4.17 s | **3.96 s** | 4.72 s | 0.84 | **3.97 s** | 4.74 s | 26.0 MB | 26.1 MB | **Xisto** |
| seidel-2d | 27.68 s | **4.86 s** | 27.67 s | 0.18 | **4.89 s** | 27.70 s | 32.5 MB | 32.6 MB | **Xisto** |
| symm | 2.01 s | **1.12 s** | 2.10 s | 0.53 | **1.14 s** | 2.13 s | 28.0 MB | 28.1 MB | **Xisto** |
| syr2k | 4.72 s | **1.07 s** | 4.06 s | 0.26 | **1.11 s** | 4.09 s | 31.4 MB | 31.5 MB | **Xisto** |
| syrk | 666 ms | **558 ms** | 669 ms | 0.83 | **0.58 s** | 0.69 s | 22.1 MB | 22.3 MB | **Xisto** |
| trisolv | 4 ms | **3 ms** | 4 ms | 0.75 | 0.03 s | 0.03 s | 32.5 MB | 32.7 MB | **Xisto** |
| trmm | 1.80 s | **268 ms** | 1.87 s | 0.14 | **0.29 s** | 1.89 s | 18.7 MB | 19.0 MB | **Xisto** |

## Graviton2 (Neoverse N1, arm64, 2 vCPUs)

Kernel time: Xisto **20 wins**, 6 ties, 4 losses (a tie is within 3%). The winner of each column pair (kernel time, CPU, memory) is in bold when ahead by more than 3%; C is the reference, not scored.

| kernel | C (ref.) kernel | Xisto kernel | Rust kernel | ratio | Xisto CPU | Rust CPU | Xisto memory | Rust memory | faster |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 2mm | 6.14 s | **1.81 s** | 5.56 s | 0.33 | **1.84 s** | 5.58 s | 37.0 MB | 37.0 MB | **Xisto** |
| 3mm | 10.07 s | **3.18 s** | 7.32 s | 0.43 | **3.22 s** | 7.36 s | 53.0 MB | 52.8 MB | **Xisto** |
| adi | 11.99 s | **10.12 s** | 11.68 s | 0.87 | **10.12 s** | 11.70 s | 32.5 MB | 32.3 MB | **Xisto** |
| atax | 6 ms | 6 ms | 6 ms | 1.00 | 0.03 s | 0.03 s | 31.9 MB | 32.2 MB | tie |
| bicg | 14 ms | 4 ms | 4 ms | 1.00 | 0.03 s | 0.03 s | 32.0 MB | 32.2 MB | tie |
| cholesky | 1.90 s | **1.60 s** | 1.94 s | 0.82 | **13.89 s** | 45.18 s | 62.7 MB | 62.8 MB | **Xisto** |
| correlation | 7.44 s | **2.10 s** | 7.14 s | 0.29 | **2.11 s** | 7.15 s | 25.4 MB | 25.6 MB | **Xisto** |
| covariance | 7.49 s | **2.07 s** | 7.15 s | 0.29 | **2.09 s** | 7.17 s | 25.4 MB | 25.6 MB | **Xisto** |
| deriche | 321 ms | **203 ms** | 303 ms | 0.67 | **0.28 s** | 0.36 s | 139.4 MB | 136.9 MB | **Xisto** |
| doitgen | 628 ms | **209 ms** | 526 ms | 0.40 | **0.23 s** | 0.55 s | 29.3 MB | **27.4 MB** | **Xisto** |
| durbin | 3 ms | 5 ms | **3 ms** | 1.67 | 0.01 s | **0.00 s** | **1.3 MB** | 1.8 MB | **Rust** |
| fdtd-2d | 1.88 s | **1.88 s** | 1.97 s | 0.95 | **1.90 s** | 1.99 s | 29.3 MB | 29.2 MB | **Xisto** |
| floyd-warshall | 23.11 s | 25.02 s | **23.87 s** | 1.05 | 25.10 s | **23.94 s** | 31.7 MB | 31.7 MB | **Rust** |
| gemm | 618 ms | 657 ms | **619 ms** | 1.06 | 0.69 s | **0.64 s** | 29.3 MB | 29.3 MB | **Rust** |
| gemver | 39 ms | **19 ms** | 37 ms | 0.51 | **0.05 s** | 0.06 s | 32.1 MB | 32.5 MB | **Xisto** |
| gesummv | 6 ms | 2 ms | 2 ms | 1.00 | 0.02 s | 0.02 s | 27.3 MB | 27.6 MB | tie |
| gramschmidt | 13.45 s | **2.62 s** | 15.44 s | 0.17 | **2.64 s** | 15.46 s | 30.9 MB | 31.0 MB | **Xisto** |
| heat-3d | 3.52 s | **3.66 s** | 3.97 s | 0.92 | **3.68 s** | 3.98 s | 30.8 MB | **28.0 MB** | **Xisto** |
| jacobi-1d | 1 ms | 1 ms | 1 ms | 1.00 | 0.00 s | 0.00 s | **1.3 MB** | 1.8 MB | tie |
| jacobi-2d | 1.91 s | 1.92 s | 1.90 s | 1.01 | 1.94 s | 1.92 s | 27.4 MB | 27.6 MB | tie |
| lu | 12.30 s | **5.26 s** | 13.84 s | 0.38 | **17.54 s** | 57.26 s | 62.7 MB | 62.8 MB | **Xisto** |
| ludcmp | 10.40 s | **5.06 s** | 10.53 s | 0.48 | **17.24 s** | 54.09 s | 62.7 MB | 62.8 MB | **Xisto** |
| mvt | 34 ms | **13 ms** | 24 ms | 0.54 | **0.04 s** | 0.05 s | 32.1 MB | 32.3 MB | **Xisto** |
| nussinov | 9.92 s | **7.71 s** | 8.85 s | 0.87 | **7.72 s** | 8.85 s | 25.5 MB | 25.6 MB | **Xisto** |
| seidel-2d | 20.63 s | **6.76 s** | 20.54 s | 0.33 | **6.78 s** | 20.56 s | 32.0 MB | 32.3 MB | **Xisto** |
| symm | 4.59 s | 5.08 s | **4.54 s** | 1.12 | 5.10 s | **4.56 s** | 27.7 MB | 27.6 MB | **Rust** |
| syr2k | 5.99 s | **2.76 s** | 6.28 s | 0.44 | **2.79 s** | 6.30 s | 31.2 MB | 31.0 MB | **Xisto** |
| syrk | 2.04 s | **1.00 s** | 1.81 s | 0.55 | **1.02 s** | 1.83 s | 21.7 MB | 21.8 MB | **Xisto** |
| trisolv | 3 ms | 3 ms | 3 ms | 1.00 | 0.02 s | 0.02 s | 32.1 MB | 32.3 MB | tie |
| trmm | 3.91 s | **627 ms** | 3.94 s | 0.16 | **0.64 s** | 3.95 s | 18.3 MB | 18.6 MB | **Xisto** |

## Notes

- **floyd-warshall and heat-3d** were improved after this round (`b0c603b`): floyd-warshall 14.1 s on the Graviton2 and 10.6 s on the Xeon, heat-3d 1.83 s on the Xeon; Xisto now wins both on both machines. See [PolyBench](./index.md#the-last-two-fixed-after-the-round-b0c603b).

- **symm and gemm on the Graviton2** show as losses in this round and tie when re-measured with the sides interleaved in one invocation (symm 4.53 s vs 4.59 s, gemm 0.646 s vs 0.642 s; `paired-symm-gemm-durbin-graviton2.txt`).
- **durbin** (5 ms vs 3–4 ms here) ties after `479973f`, which proves `r[k - i - 1]` in range once per loop: 3 ms on the Graviton2, 4 ms on the Xeon, at the timer's millisecond resolution.
- Rust's arrays are one block per matrix; Xisto's `number[][]` rows are separate allocations, as in any TypeScript program. The kernels where Xisto wins by 3–7× (2mm, 3mm, correlation, covariance, seidel-2d...) are loop transformations of the compiler, not a different algorithm: the same operations in the same order per element.

Raw output: [bench/results/polybench-2026-11-14](https://github.com/xistolang/xisto/tree/main/bench/results/polybench-2026-11-14).
