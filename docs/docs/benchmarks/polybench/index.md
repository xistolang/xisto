---
title: PolyBench
sidebar_position: 2
---

# PolyBench: Xisto vs Rust and Go

14 Nov 2026, main `40cdc16`, on the two
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

One page per contestant, with kernel time, CPU and memory per kernel and the C reference beside them:
[Xisto vs Rust](./vs-rust.md), [Xisto vs Go](./vs-go.md), [Xisto vs Node](./vs-node.md),
[Xisto vs Bun](./vs-bun.md). Node and Bun run the very same `.ts` files Xisto compiles: Xisto wins all 30 kernels
against both on both VMs (15 Nov), 3× to 33× faster than Node and 1.4× to 25× faster than Bun.


| machine | Xisto wins | ties | losses |
|---|---:|---:|---:|
| Xeon (x86-64) | **21** | 9 | 0 |
| Graviton2 (ARM) | **21** | 9 | 0 |

Four kernels came out close in the round and were re-measured with the four sides interleaved in one invocation
(marked ² in the tables, which show the interleaved times): on the Xeon fdtd-2d (1.62 s vs Rust 1.64 s), atax and
durbin (8 ms and 4 ms on Xisto and Rust, at the timer's millisecond resolution); on the Graviton2 gemm (633 ms vs Rust
630 ms). All four are ties.

### Since the previous round

| kernel | machine | before¹ | now | Rust | what changed |
|---|---|---:|---:|---:|---|
| floyd-warshall | Graviton2 | 25.0 s | **14.0 s** | 23.7 s | `path[i][k]` read once before the j loop, which then vectorizes |
| floyd-warshall | Xeon | 18.8 s | **10.5 s** | 18.8 s | the same |
| heat-3d | Xeon | 2.17 s | **1.79 s** | 2.00 s | the two sweeps of a time step fused one plane and one row apart |
| jacobi-2d | Xeon | 1.51 s | **1.24 s** | 1.41 s | the same fusion for two-dimensional stencils, one row apart |
| symm | Graviton2 | 5.08 s | **4.25 s** | 4.52 s | rows on 64-byte lines and two columns jammed instead of sixteen |

¹ The round of main `9a6c2d3` (`bench/results/polybench-2026-11-14/*-9a6c2d3.txt`).

- **floyd-warshall**: at j = k the loop writes `min(path[i][k], path[i][k] + path[k][k])`; when `path[k][k] >= 0`
  that writes the same value, so `path[i][k]` is read once before the j loop (checked once per entry, with the rows
  apart; otherwise the loop runs as written).
- **heat-3d and jacobi-2d** (x86 only): row c of the second sweep's input is ready once row c+1 of the first sweep is
  written, so the compiler interleaves them; each element keeps its expression and operands, so the bits are the
  same, and the rows in use stay in the L2 cache.
- **symm** (ARM Linux only): with sixteen rows read at once from arbitrary heap offsets, its time moved by 12% with
  the heap's layout alone (one more command-line argument shifted it); rows on 64-byte lines and a jam of two
  columns make it steady and faster.

**Xisto loses no kernel on either machine.**

## Why Xisto wins most of them

The Xisto compiler proves the loops' indices in range once per loop (with a fallback to the loop as written), and
then applies loop transformations that a C compiler does not do on its own: unroll-and-jam of matrix products
(several output columns per pass, held in registers), skewed Gauss-Seidel (seidel-2d: 12 rows at a time), lanes for
recurrences (adi, deriche), selection reductions with several chains (nussinov), and a stencil nest with the row
pointers in a table. Every transformation keeps the order of every floating-point operation, which is why the
hashes match the C's bit for bit. On ARM Linux the runtime also places matrix rows an odd number of cache lines
apart, so the Neoverse N1's stride prefetcher can follow a column (adi 13.5 s before it, 10.35 s in this round).

## Results (kernel time)

The fastest of Xisto, Rust and Go in bold. ² re-measured interleaved (see above).

### Xeon E5-2686 v4 (x86-64)

| kernel | C (ref.) | Xisto | Rust | Go | Xisto vs best |
|---|---:|---:|---:|---:|---|
| 2mm | 2.73 s | **620 ms** | 2.67 s | 5.84 s | **win** |
| 3mm | 4.28 s | **1.04 s** | 4.07 s | 9.51 s | **win** |
| adi | 12.44 s | **6.28 s** | 14.03 s | 15.72 s | **win** |
| atax² | 7 ms | **8 ms** | **8 ms** | 18 ms | tie |
| bicg | 14 ms | **7 ms** | **7 ms** | 14 ms | tie |
| cholesky | 2.00 s | **1.98 s** | 2.00 s | 4.43 s | tie |
| correlation | 7.19 s | **724 ms** | 7.26 s | 6.96 s | **win** |
| covariance | 7.19 s | **742 ms** | 7.31 s | 7.05 s | **win** |
| deriche | 463 ms | **165 ms** | 401 ms | 470 ms | **win** |
| doitgen | 740 ms | **97 ms** | 524 ms | 1.66 s | **win** |
| durbin² | 4 ms | **4 ms** | **4 ms** | 5 ms | tie |
| fdtd-2d² | 1.63 s | **1.62 s** | 1.64 s | 3.98 s | tie |
| floyd-warshall | 18.10 s | **10.46 s** | 18.75 s | 37.13 s | **win** |
| gemm | 444 ms | 434 ms | **422 ms** | 1.16 s | tie |
| gemver | 29 ms | **10 ms** | 29 ms | 44 ms | **win** |
| gesummv | 6 ms | **3 ms** | **3 ms** | 6 ms | tie |
| gramschmidt | 7.21 s | **1.40 s** | 7.35 s | 6.37 s | **win** |
| heat-3d | 1.88 s | **1.79 s** | 2.00 s | 6.75 s | **win** |
| jacobi-1d | 1 ms | **1 ms** | **1 ms** | 3 ms | tie |
| jacobi-2d | 1.30 s | **1.24 s** | 1.41 s | 3.91 s | **win** |
| lu | 5.50 s | **2.10 s** | 5.57 s | 10.31 s | **win** |
| ludcmp | 5.24 s | **2.23 s** | 5.46 s | 5.91 s | **win** |
| mvt | 25 ms | **6 ms** | 25 ms | 35 ms | **win** |
| nussinov | 4.13 s | **4.08 s** | 4.58 s | 9.26 s | **win** |
| seidel-2d | 27.68 s | **4.85 s** | 27.67 s | 31.38 s | **win** |
| symm | 1.98 s | **1.12 s** | 2.21 s | 2.74 s | **win** |
| syr2k | 4.24 s | **1.07 s** | 4.04 s | 4.26 s | **win** |
| syrk | 648 ms | **553 ms** | 642 ms | 1.06 s | **win** |
| trisolv | 3 ms | **4 ms** | **4 ms** | 7 ms | tie |
| trmm | 1.79 s | **275 ms** | 1.80 s | 2.34 s | **win** |

### Graviton2 (Neoverse N1, ARM)

| kernel | C (ref.) | Xisto | Rust | Go | Xisto vs best |
|---|---:|---:|---:|---:|---|
| 2mm | 6.21 s | **1.68 s** | 5.32 s | 9.58 s | **win** |
| 3mm | 10.11 s | **2.97 s** | 7.27 s | 17.44 s | **win** |
| adi | 12.12 s | **10.35 s** | 11.42 s | 15.64 s | **win** |
| atax | 6 ms | **6 ms** | **6 ms** | 19 ms | tie |
| bicg | 14 ms | **4 ms** | **4 ms** | 15 ms | tie |
| cholesky | 1.88 s | **1.58 s** | 1.97 s | 4.47 s | **win** |
| correlation | 7.55 s | **2.06 s** | 7.11 s | 7.75 s | **win** |
| covariance | 7.43 s | **2.05 s** | 7.13 s | 7.75 s | **win** |
| deriche | 330 ms | **195 ms** | 287 ms | 364 ms | **win** |
| doitgen | 657 ms | **209 ms** | 523 ms | 1.61 s | **win** |
| durbin | 3 ms | **3 ms** | **3 ms** | 6 ms | tie |
| fdtd-2d | 1.78 s | **1.90 s** | 1.94 s | 4.76 s | tie |
| floyd-warshall | 23.08 s | **14.01 s** | 23.73 s | 60.72 s | **win** |
| gemm² | 642 ms | 633 ms | **630 ms** | 4.83 s | tie |
| gemver | 39 ms | **20 ms** | 37 ms | 51 ms | **win** |
| gesummv | 6 ms | **2 ms** | **2 ms** | 6 ms | tie |
| gramschmidt | 13.25 s | **2.61 s** | 15.57 s | 13.80 s | **win** |
| heat-3d | 3.50 s | **3.66 s** | 3.98 s | 8.84 s | **win** |
| jacobi-1d | 1 ms | **1 ms** | **1 ms** | 4 ms | tie |
| jacobi-2d | 1.96 s | 1.96 s | **1.92 s** | 5.89 s | tie |
| lu | 12.41 s | **5.22 s** | 13.90 s | 14.10 s | **win** |
| ludcmp | 10.48 s | **5.16 s** | 10.65 s | 8.76 s | **win** |
| mvt | 35 ms | **13 ms** | 24 ms | 38 ms | **win** |
| nussinov | 9.91 s | **7.72 s** | 10.38 s | 17.80 s | **win** |
| seidel-2d | 20.61 s | **6.86 s** | 20.55 s | 24.45 s | **win** |
| symm | 4.58 s | **4.25 s** | 4.52 s | 5.31 s | **win** |
| syr2k | 5.98 s | **2.74 s** | 6.33 s | 6.71 s | **win** |
| syrk | 2.02 s | **1.06 s** | 1.90 s | 3.25 s | **win** |
| trisolv | 3 ms | **3 ms** | **3 ms** | 7 ms | tie |
| trmm | 3.90 s | **650 ms** | 3.95 s | 4.43 s | **win** |

The same rows with CPU and memory: [Xisto vs Rust](./vs-rust.md), [Xisto vs Go](./vs-go.md). Raw output (this round: `*-40cdc16.txt`), the A/B against the Benchmarks Game and the interleaved re-measurements:
[bench/results/polybench-2026-11-14](https://github.com/xistolang/xisto/tree/main/bench/results/polybench-2026-11-14).

## Reproduce

```sh
POLYBENCH_SRC=/path/to/PolyBenchC-4.2.1 python3 bench/polybench/run.py 3 c xisto rust go --dataset LARGE
```
