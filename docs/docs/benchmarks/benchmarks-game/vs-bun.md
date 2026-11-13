---
title: Xisto vs Bun
sidebar_position: 3
---

# Xisto vs Bun

13 Nov 2026, the xisto variants of main `e5fb066` (before `mandelbrot-8`), on the Apple M4 and the
two AWS VMs.

## Setup

- **Bun programs**: the official Node programs of the Benchmarks Game, one per problem, kept in
  `bench/benchmarksgame/node/` (BSD-3): spectral-norm #1, n-body #8, fannkuch-redux #8, binary-trees #7,
  mandelbrot #1, fasta #1, k-nucleotide #8, reverse-complement #2, regex-redux #4, pidigits #2 and #3.
  They run on one thread.
- **Runtime**: Bun 1.4.4 on all three machines (the official release zips on the VMs, checked against
  `SHASUMS256.txt`). Bun runs a `.cjs` copy of each program, because some originals assign undeclared names
  (`f = 0`), which an ES module refuses.
- **xisto**: the same variants as the Rust round, built with `XISTO_CPU=native`. The xisto variants and what each does are listed on [Xisto vs Rust](./vs-rust.md#the-xisto-programs).
- **Command**: `python3 bench/benchmarksgame/langs.py 3 node=js:NODE bun=js:BUN` runs both runtimes in one round,
  scored with `python3 bench/placar.py RESULTS bun` (see
  [How every run is measured](./index.md#how-every-run-is-measured)).
- **Steal** during the round: +596 on the Xeon, +2,566 on the Graviton2 (the same round as Node).

Most xisto variants run on every core (10 on the M4, 2 on the VMs), while the Bun programs run on one. The time
ratio therefore mixes a faster program with more cores; the CPU columns compare the work done. The ratio in the
result column is Bun's time divided by xisto's.

## Apple M4 (10 cores)

Source: `bench/results/node-bun-mac-m4-2026-11-13-e5fb066.txt`.

| problem | xisto | Bun | xisto time | Bun time | xisto CPU | Bun CPU | xisto mem | Bun mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| spectral-norm | spectralnorm-6 | spectralnorm-1 | 0.069 s | 1.312 s | 0.53 s | 1.32 s | 2.7 MB | 20 MB | win, 19.0× |
| n-body | nbody | nbody-8 | 1.124 s | 2.487 s | 1.12 s | 2.49 s | 1.2 MB | 19 MB | win, 2.2× |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux-8 | 1.627 s | 25.868 s | 15.53 s | 25.86 s | 3.6 MB | 19 MB | win, 15.9× |
| binary-trees | binarytrees-2 † | binarytrees-7 | 0.246 s | 3.450 s | 1.87 s | 4.27 s | 120 MB | 1,194 MB | win, 14.0× |
| mandelbrot | mandelbrot-6 | mandelbrot-1 | 0.258 s | 22.357 s | 2.42 s | 22.20 s | 9.4 MB | 25 MB | win, 86.7× |
| fasta | fasta | fasta-1 | 0.232 s | 5.518 s | 0.16 s | 5.50 s | 2.3 MB | 85 MB | win, 23.8× |
| k-nucleotide | knucleotide-7 | knucleotide-8 | 0.224 s | 26.613 s | 1.62 s | 26.67 s | 127 MB | 1,957 MB | win, 118.8× |
| reverse-complement | revcomp-9 | revcomp-2 | 0.124 s | 1.708 s | 0.10 s | 1.74 s | 12 MB | 282 MB | win, 13.8× |
| regex-redux | regexredux-2 | regexredux-4 | 0.259 s | 0.344 s | 0.37 s | 0.34 s | 242 MB | 364 MB | win, 1.3× |
| pidigits | pidigits | pidigits-3 | 0.252 s | 0.727 s | 0.25 s | 0.88 s | 1.9 MB | 99 MB | win, 2.9× |

## Xeon E5-2686 v4 (2 vCPUs)

Source: `bench/results/vm-2026-11-13/node-bun-ubuntu-xeon-2026-11-13-e5fb066.txt`.

| problem | xisto | Bun | xisto time | Bun time | xisto CPU | Bun CPU | xisto mem | Bun mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| spectral-norm | spectralnorm-6 | spectralnorm-1 | 0.459 s | 4.164 s | 0.89 s | 4.19 s | 2.3 MB | 34 MB | win, 9.1× |
| n-body | nbody | nbody-8 | 2.456 s | 8.155 s | 2.45 s | 8.16 s | 2.0 MB | 36 MB | win, 3.3× |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux-8 | 6.596 s | 55.129 s | 13.11 s | 55.13 s | 2.3 MB | 34 MB | win, 8.4× |
| binary-trees | binarytrees † | binarytrees-7 | 1.624 s | 13.236 s | 3.05 s | 16.76 s | 70 MB | 1,067 MB | win, 8.2× |
| mandelbrot | mandelbrot-6 | mandelbrot-1 | 1.327 s | 72.575 s | 2.53 s | 72.33 s | 7.3 MB | 42 MB | win, 54.7× |
| fasta | fasta | fasta-1 | 1.074 s | 15.463 s | 0.29 s | 15.34 s | 2.4 MB | 101 MB | win, 14.4× |
| k-nucleotide | knucleotide-7 † | knucleotide-8 | 1.384 s | 96.810 s | 2.68 s | 97.45 s | 69 MB | 1,690 MB | win, 69.9× |
| reverse-complement | | | | | | | | | no row: Bun's output differed |
| regex-redux | regexredux-2 | regexredux-4 | 0.690 s | 1.146 s | 0.97 s | 1.16 s | 149 MB | 327 MB | win, 1.7× |
| pidigits | pidigits | pidigits-3 | 0.625 s | 2.340 s | 0.62 s | 2.37 s | 2.6 MB | 109 MB | win, 3.7× |

## Graviton2 (2 vCPUs)

Source: `bench/results/vm-2026-11-13/node-bun-arm-graviton2-2026-11-13-e5fb066.txt`.

| problem | xisto | Bun | xisto time | Bun time | xisto CPU | Bun CPU | xisto mem | Bun mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| spectral-norm | spectralnorm-7 | spectralnorm-1 | 1.217 s | 4.777 s | 2.43 s | 4.81 s | 1.8 MB | 22 MB | win, 3.9× |
| n-body | nbody | nbody-8 | 4.610 s | 14.418 s | 4.60 s | 14.43 s | 1.6 MB | 22 MB | win, 3.1× |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux-8 | 8.575 s | 64.664 s | 17.13 s | 64.63 s | 1.8 MB | 23 MB | win, 7.5× |
| binary-trees | binarytrees-3 | binarytrees-7 | 1.832 s | 13.141 s | 3.60 s | 16.97 s | 114 MB | 1,127 MB | win, 7.2× |
| mandelbrot | mandelbrot-7 | mandelbrot-1 | 2.744 s | 59.308 s | 5.45 s | 58.59 s | 6.8 MB | 32 MB | win, 21.6× |
| fasta | fasta | fasta-1 | 0.544 s | 16.264 s | 0.38 s | 16.23 s | 1.9 MB | 92 MB | win, 29.9× |
| k-nucleotide | knucleotide-7 | knucleotide-8 | 3.011 s | 149.910 s | 5.90 s | 150.17 s | 64 MB | 1,329 MB | win, 49.8× |
| reverse-complement | revcomp-6 † | revcomp-2 | 0.282 s | 3.810 s | 0.24 s | 3.90 s | 188 MB | 293 MB | win, 13.5× |
| regex-redux | regexredux-2 | regexredux-4 | 0.799 s | 1.411 s | 1.25 s | 1.42 s | 149 MB | 311 MB | win, 1.8× |
| pidigits | pidigits | pidigits-2 | 2.039 s | 3.207 s | 2.04 s | 3.27 s | 2.1 MB | 88 MB | win, 1.6× |

## Notes

- **Not scored**: reverse-complement on the Xeon, where Bun's output differed; on the M4 and the Graviton2 it matched.
- **Closest row**: regex-redux, where Bun's regular expressions are fast: 1.3× on the M4, 1.7× on the Xeon, 1.8× on
  the Graviton2. On the M4 Bun uses less CPU there (0.34 s against xisto's 0.37 s), the only row of the three pages
  where the other side uses less CPU.
- **pidigits**: Bun's `BigInt` is much faster than Node's (0.73 s against 5.57 s on the M4); xisto stays ahead by
  1.6× to 3.7×.
- **Memory**: Bun starts lower than Node (~20–35 MB) but reaches 1.1–2 GB on binary-trees and k-nucleotide.
