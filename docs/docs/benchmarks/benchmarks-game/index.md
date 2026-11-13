---
title: Benchmarks Game
sidebar_position: 2
---

# Benchmarks Game

The [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) has ten
problems with official programs in many languages. The xisto ports live in `bench/benchmarksgame/`: `NAME.ts` is
the plain program with types, and `NAME-2.ts`, `NAME-3.ts`, ... are optimized variants, some of them parallel
through `xisto:parallel` or `worker_threads`. Every output is checked bit for bit against Node's.

## Results (13 Nov 2026, main `e5fb066`)

| comparison | Apple M4 | Xeon E5-2686 v4 | Graviton2 | page |
|---|---|---|---|---|
| xisto vs Rust | — | 9 wins, 1 tie | 8 wins, 2 ties | [xisto vs Rust](./vs-rust.md) |
| xisto vs Node 26 | 10 wins | 9 wins (fasta not scored) | 9 wins (fasta not scored) | [xisto vs Node](./vs-node.md) |
| xisto vs Bun 1.4 | 10 wins | 9 wins (reverse-complement not scored) | 10 wins | [xisto vs Bun](./vs-bun.md) |

No problem is lost anywhere, and xisto's peak memory is at or below the other side's on every scored row. Each page
has the setup of its test, the tables per machine and the notes on each problem.

## The ten problems

| problem | size | input | what it measures |
|---|---|---|---|
| spectral-norm | 5500 | — | floating-point division and sums over an n×n matrix computed on the fly |
| n-body | 50,000,000 steps | — | floating-point arithmetic and `sqrt` on five bodies |
| fannkuch-redux | 12 | — | permutations of small arrays, integer work |
| binary-trees | 21 | — | allocating and freeing millions of small objects |
| mandelbrot | 16000 | — | floating-point loops, output of a 32 MB bitmap |
| fasta | 25,000,000 | — | random number generation and output (250 MB) |
| k-nucleotide | — | 250 MB (fasta 25,000,000) | hash tables over every substring of length 1–18 |
| reverse-complement | — | 250 MB (fasta 25,000,000) | reading, transforming and writing large buffers |
| regex-redux | — | 50 MB (fasta 5,000,000) | regular expressions over a large string |
| pidigits | 10000 | — | arbitrary-precision integers (GMP in C, Rust, Node `BigInt`) |

## How every run is measured

- **Runner**: `bench/benchmarksgame/langs.py`, from the repository root. For each problem it builds every xisto
  variant (`NAME.ts`, `NAME-2.ts`, ...) with `build/xisto build`, then runs each program of each side: one warm-up
  run, then 3 runs, and keeps the median wall time, the median CPU time and the highest peak RSS.
- **Measurement**: `bench/measure.py`. Wall time from `time.perf_counter` around the whole run; CPU time (user +
  system) and peak RSS from `wait4` on the child (on Linux through a small C launcher, `bench/rusage.c`, so the
  child's RSS does not start at Python's).
- **Output check**: stdout goes through a pipe, is read whole and hashed (SHA-256). A program whose hash differs from
  xisto's is shown as `DIFFERS` and never scored; xisto's own output is checked against Node's by
  `bench/benchmarksgame/run.py` and by the test suite.
- **Inputs**: the stdin files are made once with xisto's fasta (`bench/benchmarksgame/out/fasta-25000000.txt` and
  `fasta-5000000.txt`) and read from the page cache.
- **Scoring**: `bench/placar.py RESULTS [rust|node|bun]` picks each side's fastest variant whose output matched, or
  a lighter one when it is close: up to 2% slower with at least 10% less memory, or 2–5% slower with at least 25%
  less (marked †). A row is a tie when the times are within 5%.
- **Builds**: xisto with `XISTO_CPU=native` (the default binary is portable; Rust builds with `target-cpu=native`).
- **VMs**: AWS t2.medium (Xeon E5-2686 v4, 2 vCPUs, AVX2, Ubuntu, clang 21) and t4g.small (Graviton2, Neoverse-N1,
  2 vCPUs, Amazon Linux, clang 15). Both are burstable: each result file logs the `steal` counter before and after
  the round, and close results are confirmed with 9 interleaved runs (`ab.py`) before anything is changed.

## The plain program against the optimized one (10 Nov 2026)

The plain `NAME.ts` stays within 1.10× of the best optimized xisto variant on all 8 problems that have one, on both
machines (`bench/results/placar-final-2026-11-10.md`):

| problem | Xeon: plain ÷ optimized | Graviton2: plain ÷ optimized |
|---|---:|---:|
| fasta | 0.85 | 0.52 |
| n-body | 0.95 | 0.96 |
| spectral-norm | 0.99 | 1.07 |
| reverse-complement | 1.01 | 0.90 |
| mandelbrot | 1.04 | 1.01 |
| binary-trees | 1.06 | 1.05 |
| k-nucleotide | 1.08 | 0.95 |
| fannkuch-redux | 1.09 | 1.00 |


## See also

- [Benchmarks](../index.md)
- [Methodology](../../performance/methodology.md)
- [The performance model](../../performance/model.md)
