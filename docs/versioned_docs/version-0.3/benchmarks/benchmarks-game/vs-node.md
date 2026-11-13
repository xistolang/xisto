---
title: Xisto vs Node
sidebar_position: 2
---

# Xisto vs Node

13 Nov 2026, the xisto variants of main `e5fb066` (before `mandelbrot-8`), on the Apple M4 and the
two AWS VMs.

## Setup

- **Node programs**: the official Node programs of the Benchmarks Game, one per problem, kept in
  `bench/benchmarksgame/node/` (BSD-3): spectral-norm #1, n-body #8, fannkuch-redux #8, binary-trees #7,
  mandelbrot #1, fasta #1, k-nucleotide #8, reverse-complement #2, regex-redux #4, pidigits #2 and #3.
  They run on one thread.
- **Runtime**: Node v26.12.0 on all three machines (the official Linux tarballs on the VMs, checked against
  `SHASUMS256.txt`), run as `node PROGRAM.js ARG` with no flags.
- **xisto**: the same variants as the Rust round, built with `XISTO_CPU=native`. The xisto variants and what each does are listed on [Xisto vs Rust](./vs-rust.md#the-xisto-programs).
- **Command**: `python3 bench/benchmarksgame/langs.py 3 node=js:NODE bun=js:BUN` runs both runtimes in one round,
  scored with `python3 bench/placar.py RESULTS node` (see
  [How every run is measured](./index.md#how-every-run-is-measured)).
- **Steal** during the round: +596 on the Xeon, +2,566 on the Graviton2 (mostly in the long Node and Bun runs).

Most xisto variants run on every core (10 on the M4, 2 on the VMs), while the Node programs run on one. The time
ratio therefore mixes a faster program with more cores; the CPU columns compare the work done. The ratio in the
result column is Node's time divided by xisto's.

## Apple M4 (10 cores)

Source: `bench/results/node-bun-mac-m4-2026-11-13-e5fb066.txt`.

| problem | xisto | Node | xisto time | Node time | xisto CPU | Node CPU | xisto mem | Node mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| spectral-norm | spectralnorm-6 | spectralnorm-1 | 0.069 s | 1.102 s | 0.53 s | 1.11 s | 2.7 MB | 46 MB | win, 16.0× |
| n-body | nbody | nbody-8 | 1.124 s | 2.341 s | 1.12 s | 2.34 s | 1.2 MB | 45 MB | win, 2.1× |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux-8 | 1.627 s | 24.913 s | 15.53 s | 24.91 s | 3.6 MB | 43 MB | win, 15.3× |
| binary-trees | binarytrees-2 † | binarytrees-7 | 0.246 s | 5.194 s | 1.87 s | 7.00 s | 120 MB | 1,058 MB | win, 21.1× |
| mandelbrot | mandelbrot-6 | mandelbrot-1 | 0.258 s | 24.489 s | 2.42 s | 24.31 s | 9.4 MB | 50 MB | win, 94.9× |
| fasta | fasta | fasta-1 | 0.232 s | 9.926 s | 0.16 s | 9.93 s | 2.3 MB | 50 MB | win, 42.8× |
| k-nucleotide | knucleotide-7 | knucleotide-8 | 0.224 s | 45.366 s | 1.62 s | 45.68 s | 127 MB | 704 MB | win, 202.5× |
| reverse-complement | revcomp-9 | revcomp-2 | 0.124 s | 1.989 s | 0.10 s | 2.04 s | 12 MB | 700 MB | win, 16.0× |
| regex-redux | regexredux-2 | regexredux-4 | 0.259 s | 1.168 s | 0.37 s | 1.23 s | 242 MB | 963 MB | win, 4.5× |
| pidigits | pidigits | pidigits-2 | 0.252 s | 5.571 s | 0.25 s | 5.58 s | 1.9 MB | 112 MB | win, 22.1× |

## Xeon E5-2686 v4 (2 vCPUs)

Source: `bench/results/vm-2026-11-13/node-bun-ubuntu-xeon-2026-11-13-e5fb066.txt`.

| problem | xisto | Node | xisto time | Node time | xisto CPU | Node CPU | xisto mem | Node mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| spectral-norm | spectralnorm-6 | spectralnorm-1 | 0.459 s | 3.107 s | 0.89 s | 3.13 s | 2.3 MB | 57 MB | win, 6.8× |
| n-body | nbody | nbody-8 | 2.456 s | 6.228 s | 2.45 s | 6.23 s | 2.0 MB | 56 MB | win, 2.5× |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux-8 | 6.596 s | 54.671 s | 13.11 s | 54.67 s | 2.3 MB | 54 MB | win, 8.3× |
| binary-trees | binarytrees † | binarytrees-7 | 1.624 s | 34.939 s | 3.05 s | 51.56 s | 70 MB | 687 MB | win, 21.5× |
| mandelbrot | mandelbrot-6 | mandelbrot-1 | 1.327 s | 83.686 s | 2.53 s | 83.51 s | 7.3 MB | 59 MB | win, 63.1× |
| fasta | | | | | | | | | no row: Node's output differed |
| k-nucleotide | knucleotide-7 † | knucleotide-8 | 1.384 s | 135.189 s | 2.68 s | 135.81 s | 69 MB | 674 MB | win, 97.7× |
| reverse-complement | revcomp-9 | revcomp-2 | 0.842 s | 5.301 s | 0.26 s | 4.73 s | 11 MB | 677 MB | win, 6.3× |
| regex-redux | regexredux-2 | regexredux-4 | 0.690 s | 3.967 s | 0.97 s | 4.12 s | 149 MB | 635 MB | win, 5.7× |
| pidigits | pidigits | pidigits-3 | 0.625 s | 10.891 s | 0.62 s | 10.93 s | 2.6 MB | 121 MB | win, 17.4× |

## Graviton2 (2 vCPUs)

Source: `bench/results/vm-2026-11-13/node-bun-arm-graviton2-2026-11-13-e5fb066.txt`.

| problem | xisto | Node | xisto time | Node time | xisto CPU | Node CPU | xisto mem | Node mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| spectral-norm | spectralnorm-7 | spectralnorm-1 | 1.217 s | 4.037 s | 2.43 s | 4.06 s | 1.8 MB | 54 MB | win, 3.3× |
| n-body | nbody | nbody-8 | 4.610 s | 10.207 s | 4.60 s | 10.21 s | 1.6 MB | 54 MB | win, 2.2× |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux-8 | 8.575 s | 67.627 s | 17.13 s | 67.57 s | 1.8 MB | 52 MB | win, 7.9× |
| binary-trees | binarytrees-3 | binarytrees-7 | 1.832 s | 27.839 s | 3.60 s | 40.02 s | 114 MB | 596 MB | win, 15.2× |
| mandelbrot | mandelbrot-7 | mandelbrot-1 | 2.744 s | 71.269 s | 5.45 s | 71.21 s | 6.8 MB | 49 MB | win, 26.0× |
| fasta | | | | | | | | | no row: Node's output differed |
| k-nucleotide | knucleotide-7 | knucleotide-8 | 3.011 s | 220.184 s | 5.90 s | 220.99 s | 64 MB | 617 MB | win, 73.1× |
| reverse-complement | revcomp-6 † | revcomp-2 | 0.282 s | 4.305 s | 0.24 s | 4.36 s | 188 MB | 448 MB | win, 15.3× |
| regex-redux | regexredux-2 | regexredux-4 | 0.799 s | 4.746 s | 1.25 s | 4.95 s | 149 MB | 556 MB | win, 5.9× |
| pidigits | pidigits | pidigits-2 | 2.039 s | 12.987 s | 2.04 s | 13.04 s | 2.1 MB | 85 MB | win, 6.4× |

## Notes

- **Not scored**: fasta on both VMs. Written to a file, the official Node fasta program's output is identical to
  xisto's. Written to a pipe, as the runner reads every program, it stops after about 1 MB (some 18,000 of 4.2
  million lines) and exits with code 0 and no error; how much comes out varies from run to run, Node 24 does the
  same, and it does not happen at a tenth of the size or under `strace`. On the M4 the pipe output matched. The
  program writes one `console.log` per line; the cause inside Node is still being looked into.
- **CPU**: xisto uses less CPU than Node on every row; the largest gaps are k-nucleotide (Node's `Map` of strings
  against xisto's packed counts) and mandelbrot (scalar doubles against SIMD vectors).
- **Memory**: Node starts at ~45–60 MB before any work; binary-trees, k-nucleotide, reverse-complement and
  regex-redux reach 600 MB to 1 GB on Node.
- Before 13 Nov this page compared with Node v24 on the VMs (9 Nov) and with Node 26 on the M4 with single-threaded
  xisto programs (7 Nov); those files are still in `bench/results/`.
