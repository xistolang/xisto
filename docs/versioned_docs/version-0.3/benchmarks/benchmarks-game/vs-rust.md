---
title: Xisto vs Rust
sidebar_position: 1
---

# Xisto vs Rust

13 Nov 2026, main `e5fb066`, on the two AWS VMs. Rust is the language the Benchmarks Game's fastest programs are
usually written in, so this is the comparison that says whether xisto's generated C keeps up with a systems
language.

## Setup

- **Rust programs**: every Rust program of the official source package (BSD-3), not only the fastest, built as the
  site builds them: a Cargo project with the crates they use (typed-arena, rayon, toolshed, bumpalo, regex,
  hashbrown, ...), `opt-level = 3`, `codegen-units = 1`, `-C target-cpu=native`, GMP for pidigits
  (`bench/benchmarksgame/langs.md`). rustc 1.99.1. On arm64 the programs that use x86 SIMD do not compile
  (nbody #5/#7/#9, fannkuch-redux #6, regex-redux #7, pidigits #4).
- **Excluded**: `nbody_7` and `nbody_9` compute `1/√x` with the approximate `rsqrtps` instruction and give different
  doubles; xisto promises Node's doubles, so it is not compared against a different computation.
- **xisto**: every variant in `bench/benchmarksgame/`, built with `XISTO_CPU=native`.
- **Command**: `python3 bench/benchmarksgame/langs.py 3 rust=~/rustbg/target/release`, scored with
  `python3 bench/placar.py RESULTS` (see [How every run is measured](./index.md#how-every-run-is-measured)).
- **Steal** during the round: +216 on the Xeon, +841 on the Graviton2.

## Xeon E5-2686 v4 (AWS t2.medium, 2 vCPUs, AVX2)

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

## Graviton2 (AWS t4g.small, 2 vCPUs, arm64)

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

## The ties

- **spectral-norm**, both machines: the time is the 1.21 billion divisions `1/d` the problem asks for. A C loop doing
  only those divisions takes the same CPU as both programs (0.88 s on the Xeon, 2.42 s on the Graviton2), so no
  program can be 5% ahead without caching the matrix (~240 MB) or replacing the division with an approximate
  reciprocal. xisto is ahead by 1.2% on the Graviton2 and level on the Xeon.
- **mandelbrot on the Graviton2**: both inner loops run 7 floating-point operations per pixel per iteration on the
  two NEON pipes. xisto is 1.8% ahead in time and uses 6.9 MB against 32.5 MB.

## The xisto programs

| problem | xisto variant | what it does |
|---|---|---|
| spectral-norm | `spectralnorm` (Xeon), `spectralnorm-6`/`-7` | the products split in equal shares of rows across threads; the denominator of A carried along each row by additions; #7 joins the 40 products through `Atomics` counters instead of messages. Same divisions, same order, same doubles as the plain program |
| n-body | `nbody` | the plain program with types |
| fannkuch-redux | `fannkuchredux-5` | 720 ranges to a pool of workers; the permutation as bytes in vector registers (a flip is one shuffle); two permutations' flip chains counted in the same loop |
| binary-trees | `binarytrees`, `-2`, `-3` | trees built in parallel (`xisto:parallel` or a worker pool); nodes of 8 bytes (32-bit references), a statement's temporary trees freed right after it |
| mandelbrot | `mandelbrot-6` (Xeon), `mandelbrot-7` (Graviton2) | chunks of rows to a pool of workers; 16 (#6) or 8 (#7) pixels per call as one SIMD vector; rows written out as they finish |
| fasta | `fasta` | the plain program with types; the random sequences from `xisto:random` |
| k-nucleotide | `knucleotide-7` | the sequence at two bits per nucleotide, packed in parallel; small frames counted in arrays indexed by key, the big ones in partitions |
| reverse-complement | `revcomp-9` (or `-6`) | each sequence read backwards from the file one block at a time, blocks made by workers into a shared ring, written in order (10 MB); #6 holds one sequence at a time (188 MB) |
| regex-redux | `regexredux-2` | the nine counts in a worker while the main thread does the replacements; the 50 MB string shared with the worker, not copied |
| pidigits | `pidigits` | the plain program with types (`BigInt` on xisto's GMP-backed integers) |

## What changed since 10 Nov

The 10 Nov round had 7 wins, 3 ties on the Xeon and 8 wins, 1 tie, 1 loss (mandelbrot) on the Graviton2, with xisto
using more memory than Rust on binary-trees, fasta, k-nucleotide and reverse-complement. Between 10 and 13 Nov:

- **Compiler and runtime**, for every program: 32-bit references for classes that only point to themselves (the
  binary-trees node goes from 16 to 8 bytes); objects that cannot outlive one statement go to a region given back
  to the system after it; the order of SIMD lanes chosen per architecture; a 1 MB stdout pipe on Linux; large strings
  sent to a worker without a copy; `Int32Array` and `Atomics` (`wait`, `notify`) as in Node.
- **New variants**: `mandelbrot-6`/`-7` (lane copies, output written as rows finish), `fannkuchredux-5` (two
  permutations' flips in one loop), `knucleotide-7` (2 bits per base), `revcomp-9` (each sequence read back to
  front, one block at a time), `regexredux-2` (counts in a worker), `spectralnorm-7` (an `Atomics` barrier).

## Result files

- `bench/results/vm-2026-11-13/rust-ubuntu-xeon-2026-11-13-e5fb066.txt`
- `bench/results/vm-2026-11-13/rust-arm-graviton2-2026-11-13-e5fb066.txt`
- Earlier rounds of the same day (`*-937cb49`, `*-4a10af3`, `*-5296927`) show each step of the work above.
