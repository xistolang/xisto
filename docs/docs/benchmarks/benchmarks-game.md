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

## Against Node and Bun (13 Nov 2026)

The same xisto variants as the Rust round (main `e5fb066`, `XISTO_CPU=native`) against the official Node programs of
the Benchmarks Game (`bench/benchmarksgame/node/`) under Node v26.12.0 and Bun 1.4.4, on the same three machines,
median of 3. Each side's fastest variant whose output matched, chosen by `bench/placar.py` with the same memory
rule; the ratio in the result column is the other side's time divided by xisto's.

Most xisto variants here run on every core, while the official Node programs run on one (the M4 has 10 cores, the
VMs 2). The wall-time ratio therefore mixes a faster program with more cores; the CPU columns compare the work
done.

### Apple M4 (10 cores)

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

### Xeon E5-2686 v4 (2 vCPUs)

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

### Graviton2 (2 vCPUs)

Source: `bench/results/vm-2026-11-13/node-bun-arm-graviton2-2026-11-13-e5fb066.txt`. The steal counter rose by
2,566 during this round, mostly in the long Node and Bun runs.

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

xisto wins every row on all three machines, on time and on memory; on CPU it uses less on every row except
regex-redux against Bun on the M4 (0.37 s against 0.34 s). Missing rows: the official Node fasta program gave a
different output on both VMs (its stdout writes are asynchronous when stdout is a pipe), and Bun's
reverse-complement did on the Xeon. On the Graviton2 the placar picks `revcomp-6` (188 MB), not `revcomp-9` (10 MB),
by the memory rule's 5% limit.

## See also

- [Benchmarks](./index.md)
- [Methodology](../performance/methodology.md)
- [The performance model](../performance/model.md)
- [Divergences from TypeScript](/docs/language/divergences)
