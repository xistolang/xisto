---
title: Benchmarks Game
sidebar_position: 2
---

# Benchmarks Game

The [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) has ten
problems with official programs in many languages. The xisto ports live in `bench/benchmarksgame/`: `NAME.ts` is
the plain program with types, and `NAME-2.ts`, `NAME-3.ts`, ... are optimized variants, some of them parallel
through `xisto:parallel` or `worker_threads`. Every output is checked bit for bit against Node's. Methods and the
variant rule are in [Methodology](../performance/methodology.md).

## Against Rust (13 Nov 2026)

Each side's fastest variant whose output matched, chosen by `bench/placar.py`; median of 3 interleaved runs on
the same machine, main `e5fb066`. Time is wall time; CPU is user + system; memory is peak RSS. A tie is within 5%.
xisto builds with `XISTO_CPU=native`, as Rust builds with `target-cpu=native`.

### Xeon E5-2686 v4 (AWS t2.medium, 2 vCPUs, AVX2)

Source: `bench/results/vm-2026-11-13/rust-ubuntu-xeon-2026-11-13-e5fb066.txt`.

| problem | xisto | Rust | xisto time | Rust time | xisto CPU | Rust CPU | xisto mem | Rust mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| fasta | fasta | fasta_7 | 1.102 s | 2.050 s | 0.30 s | 2.78 s | 2.4 MB | 2.9 MB | win |
| regex-redux | regexredux-2 | regexredux_6 | 0.689 s | 1.104 s | 0.97 s | 1.24 s | 149.1 MB | 150.6 MB | win |
| k-nucleotide | knucleotide-7 † | knucleotide_6 | 1.338 s | 2.103 s | 2.62 s | 3.79 s | 71.1 MB | 138.7 MB | win |
| mandelbrot | mandelbrot-6 | mandelbrot_3 † | 1.448 s ¹ | 1.995 s | 2.75 s | 3.85 s | 7.3 MB | 11.9 MB | win |
| pidigits | pidigits | pidigits_3 | 0.625 s | 0.837 s | 0.62 s | 0.84 s | 2.7 MB | 3.0 MB | win |
| reverse-complement | revcomp-9 | revcomp | 0.837 s | 1.113 s | 0.25 s | 0.27 s | 10.9 MB | 123.4 MB | win |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux_6 | 6.622 s | 7.878 s | 13.11 s | 15.62 s | 2.3 MB | 2.3 MB | win |
| binary-trees | binarytrees † | binarytrees_2 | 1.639 s | 1.887 s | 3.08 s | 3.56 s | 70.1 MB | 130.2 MB | win |
| n-body | nbody | nbody_6 | 2.454 s | 2.706 s | 2.45 s | 2.70 s | 2.0 MB | 2.1 MB | win |
| spectral-norm | spectralnorm | spectralnorm_7 | 0.459 s | 0.458 s | 0.89 s | 0.89 s | 2.2 MB | 2.4 MB | tie |

9 wins, 1 tie, no losses; xisto's peak memory is at or below Rust's on every problem. † Chosen for memory (up to 2%
slower with at least 10% less memory, or up to 5% slower with at least 25% less). ¹ One slow run in this round; 9
interleaved runs gave 1.340 s and 2.53 s of CPU (Rust `mandelbrot_4`: 1.922 s, 32.7 MB). `nbody_9` and `nbody_7`
are excluded because they compute `1/√x` with the approximate `rsqrtps` instruction and give different doubles.

### Graviton2 (AWS t4g.small, 2 vCPUs, arm64)

Source: `bench/results/vm-2026-11-13/rust-arm-graviton2-2026-11-13-e5fb066.txt`.

| problem | xisto | Rust | xisto time | Rust time | xisto CPU | Rust CPU | xisto mem | Rust mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| fasta | fasta | fasta_7 | 0.538 s | 1.419 s | 0.39 s | 2.36 s | 1.8 MB | 2.5 MB | win |
| reverse-complement | revcomp-9 † | revcomp | 0.289 s | 0.569 s | 0.30 s | 0.36 s | 10.1 MB | 123.1 MB | win |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux_4 | 8.576 s | 14.304 s | 17.13 s | 27.99 s | 1.8 MB | 1.9 MB | win |
| regex-redux | regexredux-2 | regexredux_6 | 0.805 s | 1.290 s | 1.25 s | 1.58 s | 148.6 MB | 149.7 MB | win |
| k-nucleotide | knucleotide-7 | knucleotide_6 | 3.093 s | 4.124 s | 6.07 s | 7.90 s | 64.2 MB | 138.2 MB | win |
| binary-trees | binarytrees-3 | binarytrees_2 | 1.833 s | 2.126 s | 3.61 s | 4.08 s | 113.8 MB | 130.0 MB | win |
| n-body | nbody | nbody_2 | 4.605 s | 4.998 s | 4.60 s | 4.98 s | 1.5 MB | 1.7 MB | win |
| pidigits | pidigits | pidigits_3 | 2.044 s | 2.216 s | 2.04 s | 2.21 s | 2.1 MB | 2.9 MB | win |
| mandelbrot | mandelbrot-7 | mandelbrot_4 | 2.748 s | 2.797 s | 5.45 s | 5.49 s | 6.9 MB | 32.5 MB | tie |
| spectral-norm | spectralnorm-7 | spectralnorm_5 | 1.216 s | 1.231 s | 2.43 s | 2.45 s | 1.8 MB | 2.0 MB | tie |

8 wins, 2 ties, no losses; xisto's peak memory is below Rust's on every problem.

### The ties

- **spectral-norm**, both machines: the time is the 1.21 billion divisions `1/d` the problem asks for. A C loop doing
  only those divisions takes the same CPU as both programs (0.88 s on the Xeon, 2.42 s on the Graviton2), so no
  program can be 5% ahead without caching the matrix (~240 MB) or replacing the division with an approximate
  reciprocal. xisto is ahead by 1.2% on the Graviton2 and level on the Xeon.
- **mandelbrot on the Graviton2**: both inner loops run 7 floating-point operations per pixel per iteration on the
  two NEON pipes. xisto is 1.8% ahead in time and uses 6.9 MB against 32.5 MB.

### What changed since 10 Nov

The 10 Nov round had 7 wins, 3 ties on the Xeon and 8 wins, 1 tie, 1 loss (mandelbrot) on the Graviton2, with xisto
using more memory than Rust on binary-trees, fasta, k-nucleotide and reverse-complement. Between 10 and 13 Nov:

- **Compiler and runtime**, for every program: 32-bit references for classes that only point to themselves (the
  binary-trees node goes from 16 to 8 bytes); objects that cannot outlive one statement go to a region given back
  to the system after it; the order of SIMD lanes chosen per architecture; a 1 MB stdout pipe on Linux; large strings
  sent to a worker without a copy; `Int32Array` and `Atomics` (`wait`, `notify`) as in Node.
- **New variants**: `mandelbrot-6`/`-7` (lane copies, output written as rows finish), `fannkuchredux-5` (two
  permutations' flips in one loop), `knucleotide-7` (2 bits per base), `revcomp-9` (each sequence read back to
  front, one block at a time), `regexredux-2` (counts in a worker), `spectralnorm-7` (an `Atomics` barrier).

### The plain program against the optimized one (10 Nov 2026)

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

## Against Node v24 on the VMs (9 Nov 2026)

The fastest xisto program against the Node program from the Benchmarks Game, median of 3, Node v24.22.2. Sources:
`bench/results/vm-2026-11-07/xisto-node-ubuntu-xeon-2026-11-09.txt` and
`bench/results/vm-2026-11-07/xisto-node-arm-graviton2-2026-11-09.txt`.

Many xisto programs here run on both cores (`par` ≈ 1.9) while the Node programs run on one. The wall-time ratio
therefore mixes a faster program with a second core. The CPU ratio (Node CPU ÷ xisto CPU) is the fairer measure of
work done; both are shown.

| problem | Xeon wall | Xeon CPU | Xeon ratio (wall / CPU) | Graviton2 wall | Graviton2 CPU | Graviton2 ratio (wall / CPU) |
|---|---|---|---:|---|---|---:|
| spectral-norm | 0.46 vs 3.70 s | 0.89 vs 3.71 s | 8.0× / 4.2× | 1.27 vs 4.90 s | 2.51 vs 4.92 s | 3.9× / 2.0× |
| n-body | 2.46 vs 6.30 s | 2.46 vs 6.30 s | 2.6× / 2.6× | 4.61 vs 10.33 s | 4.61 vs 10.33 s | 2.2× / 2.2× |
| fannkuch-redux | 7.64 vs 54.86 s | 15.13 vs 54.86 s | 7.2× / 3.6× | 9.80 vs 68.92 s | 19.56 vs 68.90 s | 7.0× / 3.5× |
| binary-trees | 1.77 vs 33.57 s | 3.37 vs 50.05 s | 19.0× / 14.9× | 1.94 vs 57.28 s | 3.77 vs 84.12 s | 29.6× / 22.3× |
| mandelbrot | 2.07 vs 82.84 s | 3.85 vs 82.81 s | 40.1× / 21.5× | 3.00 vs 90.61 s | 5.90 vs 90.54 s | 30.2× / 15.3× |
| k-nucleotide | 1.32 vs 104.73 s | 2.48 vs 105.28 s | 79.1× / 42.5× | 3.19 vs 168.17 s | 6.15 vs 168.86 s | 52.6× / 27.5× |
| reverse-complement | 0.97 vs 5.46 s | 0.29 vs 4.91 s | 5.6× / 16.9× | 0.34 vs 4.35 s | 0.27 vs 4.41 s | 12.6× / 16.3× |
| regex-redux | 0.95 vs 4.74 s | 0.95 vs 4.91 s | 5.0× / 5.2× | 1.20 vs 5.62 s | 1.17 vs 5.92 s | 4.7× / 5.1× |
| pidigits | 0.63 vs 10.79 s | 0.63 vs 10.81 s | 17.1× / 17.2× | 2.05 vs 12.95 s | 2.05 vs 12.98 s | 6.3× / 6.3× |

fasta has no row: the output of Node's official program differed from the expected output on both machines.
Memory follows the same pattern, for example n-body at 1.9 MB against 53 MB on the Xeon.

The same `NAME.ts`, compiled by xisto and run directly on Node, gives these wall-time ratios (Xeon / Graviton2):
spectral-norm 11.1× / 5.9×, n-body 21.0× / 13.8×, fannkuch-redux 5.6× / 5.7×, binary-trees 8.6× / 11.1×,
mandelbrot 6.8× / 6.8×, fasta 2.8× / 5.7×, k-nucleotide 16.0× / 15.7×, reverse-complement 1.9× / 4.1×,
regex-redux 4.9× / 4.4×, pidigits 16.4× / 6.2×.

## Against Node 26 and Bun on the M4 (7 Nov 2026)

Apple M4, Node v26.12.0, Bun 1.4.4, median of 3, `bench/results/benchmarksgame-2026-11-07-mac-5.txt`. Every
xisto program here is single-threaded: these runs predate the parallel ports. The ratio is against the better
of Node and Bun.

| problem | xisto | Node | Bun | xisto vs best |
|---|---|---|---|---:|
| spectral-norm | 0.851 s, 1.6 MB | 1.102 s, 45.9 MB | 1.313 s, 20.1 MB | 1.29× faster |
| n-body | 1.604 s, 1.2 MB | 2.344 s, 45.5 MB | 2.486 s, 18.8 MB | 1.46× faster |
| fannkuch-redux | 21.475 s, 1.4 MB | 24.883 s, 43.3 MB | 25.797 s, 19.1 MB | 1.16× faster |
| binary-trees | 2.842 s, 325.5 MB | 5.167 s, 1,023.8 MB | 3.466 s, 1,197.5 MB | 1.22× faster |
| mandelbrot | 8.781 s, 1.4 MB | 24.235 s, 48.8 MB | 22.086 s, 26.6 MB | 2.52× faster |
| fasta | 4.262 s, 1.6 MB | 9.899 s, 50.2 MB | 5.476 s, 84.9 MB | 1.29× faster |
| k-nucleotide | 22.410 s, 717.5 MB | 44.651 s, 704.4 MB | 27.480 s, 1,824.0 MB | 1.23× faster |
| reverse-complement | 0.670 s, 245.2 MB | 1.994 s, 689.8 MB | 1.720 s, 286.2 MB | 2.57× faster |
| regex-redux | 0.358 s, 289.1 MB | 1.239 s, 853.8 MB | 0.354 s, 364.5 MB | 1.01× slower |
| pidigits | 0.650 s, 2.9 MB | 5.652 s, 114.0 MB | 0.728 s, 99.1 MB | 1.12× faster |

On binary-trees, Node and Bun use more CPU than wall time (garbage-collector threads): 7.15 and 4.26 s of CPU.

## See also

- [Benchmarks](./index.md)
- [Methodology](../performance/methodology.md)
- [The performance model](../performance/model.md)
- [Divergences from TypeScript](/docs/language/divergences)
