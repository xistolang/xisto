---
title: PolyBench
sidebar_position: 2
---

# PolyBench: Xisto vs Rust and Go

14 Nov 2026, main `0c439b7` (results measured on `9a6c2d3`; `durbin` below re-measured on `479973f`), on the two
AWS VMs. [PolyBench/C 4.2.1](https://github.com/MatthiasJReisinger/PolyBenchC-4.2.1) is 30 numerical kernels
(linear algebra, solvers, stencils, data mining, dynamic programming) used by compiler researchers to test loop
optimizations. The Benchmarks Game measures programs; PolyBench measures the **compiler**.

## Setup

- **Dataset**: LARGE, for every kernel.
- **Xisto**: each kernel ported to TypeScript with the same loops in the same order, arrays as `number[][]`
  (deriche uses `Float32Array`, as the C uses `float`). Source: [bench/polybench/ts](https://github.com/xistolang/xisto/tree/main/bench/polybench/ts).
- **Rust**: the community port [polybench-rs](https://github.com/JRF63/polybench-rs) (contiguous fixed-size arrays),
  release build, `-C target-cpu=native`.
- **Go**: there is no community port; a line-by-line transliteration of the C, reviewed against it
  ([bench/polybench/go](https://github.com/xistolang/xisto/tree/main/bench/polybench/go)).
- **C**: the original PolyBench/C compiled with clang 21 `-O3 -march=native`, **a reference only**, not a contestant.
- **Same arithmetic**: no fused multiply-add on any side (`-ffp-contract=off`), so every side computes the same
  doubles. Every run's array dump is hashed and compared with the C reference's: all match.
- **Time**: the kernel timer (PolyBench's own `polybench_timer`, the same region in every port), median of 3 runs;
  kernels under 50 ms take the median of 10. A **tie** is within 3%.
- **Machines**: Xeon E5-2686 v4 (Broadwell, 2 vCPU) and Graviton2 (Neoverse N1, 2 vCPU). The VMs vary up to ±10%
  between runs, so every close result was re-measured with the sides interleaved in one invocation.

## Score

| machine | Xisto wins | ties | losses |
|---|---:|---:|---:|
| Xeon (x86-64) | **21** | 8 | 1 |
| Graviton2 (ARM) | **20** | 9 | 1 |

On the Graviton2, `symm` and `gemm` came out as losses in the full round and tie when re-measured interleaved
(symm 4.53 s vs Rust 4.59 s; gemm 0.646 s vs 0.642 s); they are counted as ties.
`durbin` (5 ms vs 3 ms in this round) was fixed after it: the bounds-check proof learned `r[k - i - 1]` (`479973f`;
Graviton2 kernel 3 ms, the same as C and Rust; Xeon 4 ms vs Rust 3–4 ms, at the timer's millisecond resolution):
counted as a tie on both.

### What is left

- **heat-3d on the Xeon, 1.07× in the round** (1.02–1.06× interleaved with Rust on
  `479973f`): about half is that C and Rust know `N` at compile time; the rest is `number[][][]` keeping each row apart in memory where Rust has one block.
- **floyd-warshall on the Graviton2, 1.05×**: the same scalar loop as Rust's, comparing 64-bit integers where
  Rust compares 32-bit ones. Using 32 bits would change the result on overflow, so it stays.

## Why Xisto wins most of them

The Xisto compiler proves the loops' indices in range once per loop (with a fallback to the loop as written), and
then applies loop transformations that a C compiler does not do on its own: unroll-and-jam of matrix products
(several output columns per pass, held in registers), skewed Gauss-Seidel (seidel-2d: 12 rows at a time), lanes for
recurrences (adi, deriche), selection reductions with several chains (nussinov), and a stencil nest with the row
pointers in a table. Every transformation keeps the order of every floating-point operation, which is why the
hashes match the C's bit for bit. On ARM Linux the runtime also places matrix rows an odd number of cache lines
apart, so the Neoverse N1's stride prefetcher can follow a column (adi 13.5 → 10.2 s).

## Results (kernel time)

The fastest of Xisto, Rust and Go in bold.

### Xeon E5-2686 v4 (x86-64)

| kernel | C (ref.) | Xisto | Rust | Go | Xisto vs best |
|---|---:|---:|---:|---:|---|
| 2mm | 2.75 s | **623 ms** | 2.65 s | 5.78 s | **win** |
| 3mm | 4.26 s | **1.06 s** | 4.06 s | 9.54 s | **win** |
| adi | 12.62 s | **6.30 s** | 14.12 s | 15.79 s | **win** |
| atax | 8 ms | **8 ms** | 8 ms | 18 ms | tie |
| bicg | 14 ms | **7 ms** | 7 ms | 14 ms | tie |
| cholesky | 2.00 s | **1.98 s** | 2.00 s | 4.47 s | tie |
| correlation | 7.28 s | **728 ms** | 7.29 s | 7.16 s | **win** |
| covariance | 7.28 s | **724 ms** | 7.33 s | 7.12 s | **win** |
| deriche | 464 ms | **168 ms** | 403 ms | 469 ms | **win** |
| doitgen | 739 ms | **91 ms** | 526 ms | 1.66 s | **win** |
| durbin | 4 ms | 5 ms | **4 ms** | 5 ms | loss 1.25× |
| fdtd-2d | 1.60 s | **1.61 s** | 1.82 s | 4.08 s | **win** |
| floyd-warshall | 18.39 s | **18.83 s** | 18.93 s | 37.21 s | tie |
| gemm | 421 ms | **419 ms** | 436 ms | 1.16 s | **win** |
| gemver | 30 ms | **11 ms** | 29 ms | 44 ms | **win** |
| gesummv | 6 ms | **3 ms** | 3 ms | 6 ms | tie |
| gramschmidt | 7.71 s | **1.40 s** | 6.96 s | 6.82 s | **win** |
| heat-3d | 2.12 s | 2.17 s | **2.03 s** | 6.78 s | loss 1.07× |
| jacobi-1d | 0 ms | **1 ms** | 1 ms | 3 ms | tie |
| jacobi-2d | 1.42 s | 1.51 s | **1.48 s** | 3.97 s | tie |
| lu | 6.17 s | **2.27 s** | 5.89 s | 10.53 s | **win** |
| ludcmp | 5.74 s | **2.41 s** | 5.30 s | 5.57 s | **win** |
| mvt | 27 ms | **7 ms** | 26 ms | 35 ms | **win** |
| nussinov | 4.17 s | **3.96 s** | 4.72 s | 9.24 s | **win** |
| seidel-2d | 27.68 s | **4.86 s** | 27.67 s | 31.38 s | **win** |
| symm | 2.01 s | **1.12 s** | 2.10 s | 2.84 s | **win** |
| syr2k | 4.72 s | **1.07 s** | 4.06 s | 4.62 s | **win** |
| syrk | 666 ms | **558 ms** | 669 ms | 1.06 s | **win** |
| trisolv | 4 ms | **3 ms** | 4 ms | 7 ms | **win** |
| trmm | 1.80 s | **268 ms** | 1.87 s | 2.29 s | **win** |

### Graviton2 (Neoverse N1, ARM)

| kernel | C (ref.) | Xisto | Rust | Go | Xisto vs best |
|---|---:|---:|---:|---:|---|
| 2mm | 6.14 s | **1.81 s** | 5.56 s | 9.49 s | **win** |
| 3mm | 10.07 s | **3.18 s** | 7.32 s | 17.39 s | **win** |
| adi | 11.99 s | **10.12 s** | 11.68 s | 15.76 s | **win** |
| atax | 6 ms | **6 ms** | 6 ms | 19 ms | tie |
| bicg | 14 ms | **4 ms** | 4 ms | 15 ms | tie |
| cholesky | 1.90 s | **1.60 s** | 1.94 s | 4.47 s | **win** |
| correlation | 7.44 s | **2.10 s** | 7.14 s | 7.79 s | **win** |
| covariance | 7.49 s | **2.07 s** | 7.15 s | 7.85 s | **win** |
| deriche | 321 ms | **203 ms** | 303 ms | 376 ms | **win** |
| doitgen | 628 ms | **209 ms** | 526 ms | 1.61 s | **win** |
| durbin | 3 ms | 5 ms | **3 ms** | 6 ms | loss 1.67× |
| fdtd-2d | 1.88 s | **1.88 s** | 1.97 s | 4.78 s | **win** |
| floyd-warshall | 23.11 s | 25.02 s | **23.87 s** | 60.85 s | loss 1.05× |
| gemm | 618 ms | 657 ms | **619 ms** | 4.81 s | loss 1.06× |
| gemver | 39 ms | **19 ms** | 37 ms | 51 ms | **win** |
| gesummv | 6 ms | **2 ms** | 2 ms | 6 ms | tie |
| gramschmidt | 13.45 s | **2.62 s** | 15.44 s | 13.77 s | **win** |
| heat-3d | 3.52 s | **3.66 s** | 3.97 s | 8.80 s | **win** |
| jacobi-1d | 1 ms | **1 ms** | 1 ms | 4 ms | tie |
| jacobi-2d | 1.91 s | 1.92 s | **1.90 s** | 5.76 s | tie |
| lu | 12.30 s | **5.26 s** | 13.84 s | 13.90 s | **win** |
| ludcmp | 10.40 s | **5.06 s** | 10.53 s | 8.64 s | **win** |
| mvt | 34 ms | **13 ms** | 24 ms | 37 ms | **win** |
| nussinov | 9.92 s | **7.71 s** | 8.85 s | 17.63 s | **win** |
| seidel-2d | 20.63 s | **6.76 s** | 20.54 s | 24.45 s | **win** |
| symm | 4.59 s | 5.08 s | **4.54 s** | 5.40 s | loss 1.12× |
| syr2k | 5.99 s | **2.76 s** | 6.28 s | 6.63 s | **win** |
| syrk | 2.04 s | **1.00 s** | 1.81 s | 3.21 s | **win** |
| trisolv | 3 ms | **3 ms** | 3 ms | 7 ms | tie |
| trmm | 3.91 s | **627 ms** | 3.94 s | 4.39 s | **win** |

Raw output, the A/B against the Benchmarks Game and the interleaved re-measurements:
[bench/results/polybench-2026-11-14](https://github.com/xistolang/xisto/tree/main/bench/results/polybench-2026-11-14).

## Reproduce

```sh
POLYBENCH_SRC=/path/to/PolyBenchC-4.2.1 python3 bench/polybench/run.py 3 c xisto rust go --dataset LARGE
```
