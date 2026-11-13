---
title: Xisto vs Rust
sidebar_position: 1
---

# Xisto vs Rust

13 Nov 2026, main `4a2f388`, on the two AWS VMs. Rust is the language the Benchmarks Game's fastest programs are
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
- **Excluded xisto variant**: `spectralnorm-8` takes `1/d` from a float32 division refined to the same double. It is
  measured (0.422 s on the Xeon, 0.874 s on the Graviton2) but not scored: the problem asks for the double
  division, and the scoreboard keeps the problem as stated.
- **Steal** during the round: +229 on the Xeon, +638 on the Graviton2.

## Xeon E5-2686 v4 (AWS t2.medium, 2 vCPUs, AVX2)

Source: `bench/results/vm-2026-11-13/rust-ubuntu-xeon-2026-11-13-4a2f388.txt`.

| problem | xisto | Rust | xisto time | Rust time | xisto CPU | Rust CPU | xisto mem | Rust mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| fasta | fasta | fasta_7 | 1.124 s | 1.989 s | 0.34 s | 2.66 s | 2.4 MB | 2.9 MB | win |
| regex-redux | regexredux-2 | regexredux_6 | 0.691 s | 1.106 s | 0.96 s | 1.24 s | 149.1 MB | 150.5 MB | win |
| k-nucleotide | knucleotide-7 † | knucleotide_6 | 1.358 s | 2.106 s | 2.66 s | 3.82 s | 68.7 MB | 138.6 MB | win |
| mandelbrot | mandelbrot-6 | mandelbrot_3 † | 1.339 s | 1.996 s | 2.53 s | 3.85 s | 7.3 MB | 12.1 MB | win |
| pidigits | pidigits | pidigits_3 | 0.626 s | 0.839 s | 0.62 s | 0.84 s | 2.6 MB | 3.0 MB | win |
| reverse-complement | revcomp-10 | revcomp | 0.836 s | 1.105 s | 0.24 s | 0.25 s | 10.9 MB | 123.4 MB | win |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux_6 | 6.621 s | 7.864 s | 13.08 s | 15.63 s | 2.3 MB | 2.2 MB | win |
| binary-trees | binarytrees † | binarytrees_2 | 1.631 s | 1.897 s | 3.07 s | 3.57 s | 70.1 MB | 130.2 MB | win |
| n-body | nbody | nbody_6 | 2.453 s | 2.703 s | 2.45 s | 2.70 s | 2.0 MB | 2.0 MB | win |
| spectral-norm | spectralnorm | spectralnorm_7 | 0.460 s | 0.462 s | 0.89 s | 0.89 s | 2.2 MB | 2.4 MB | tie |

9 wins, 1 tie, no losses. xisto's peak memory is at or below Rust's on every problem but fannkuch-redux, 2.3 MB
against 2.2 MB in this round (2.3 against 2.3–2.4 MB in the other rounds of the day). † Chosen for memory (up to 2%
slower with at least 10% less memory, or up to 5% slower with at least 25% less). `nbody_9` and `nbody_7` are
excluded because they compute `1/√x` with the approximate `rsqrtps` instruction and give different doubles.

## Graviton2 (AWS t4g.small, 2 vCPUs, arm64)

Source: `bench/results/vm-2026-11-13/rust-arm-graviton2-2026-11-13-4a2f388.txt`.

| problem | xisto | Rust | xisto time | Rust time | xisto CPU | Rust CPU | xisto mem | Rust mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| fasta | fasta | fasta_2 | 0.546 s | 1.839 s | 0.38 s | 3.06 s | 1.8 MB | 2.5 MB | win |
| reverse-complement | revcomp-10 | revcomp | 0.282 s | 0.564 s | 0.28 s | 0.36 s | 10.1 MB | 123.2 MB | win |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux_4 | 8.586 s | 14.326 s | 17.14 s | 27.91 s | 1.8 MB | 1.9 MB | win |
| regex-redux | regexredux-2 | regexredux_6 | 0.798 s | 1.291 s | 1.25 s | 1.58 s | 148.6 MB | 149.7 MB | win |
| k-nucleotide | knucleotide-7 | knucleotide_6 | 2.986 s | 4.319 s | 5.89 s | 7.98 s | 64.6 MB | 138.3 MB | win |
| binary-trees | binarytrees-3 | binarytrees_2 | 1.826 s | 2.190 s | 3.57 s | 4.20 s | 113.8 MB | 130.0 MB | win |
| pidigits | pidigits | pidigits_3 | 2.042 s | 2.217 s | 2.04 s | 2.21 s | 2.1 MB | 2.8 MB | win |
| n-body | nbody | nbody_2 | 4.602 s | 4.985 s | 4.60 s | 4.98 s | 1.5 MB | 1.6 MB | win |
| mandelbrot | mandelbrot-8 | mandelbrot_4 | 2.627 s | 2.792 s | 5.21 s | 5.49 s | 6.9 MB | 32.4 MB | win |
| spectral-norm | spectralnorm-7 | spectralnorm_5 | 1.220 s | 1.230 s | 2.43 s | 2.45 s | 1.8 MB | 2.1 MB | tie |

9 wins, 1 tie, no losses; xisto's peak memory is below Rust's on every problem. Reverse-complement is
`revcomp-10`, the fastest xisto variant here with 10 MB: it reads each sequence backwards one block at a time and
moves the newlines to their output places before the reverse, so each line is one run.

## The ties

**spectral-norm**, both machines: the time is the 1.21 billion double divisions `1/d` the problem asks for. A C
loop doing only those divisions takes the same CPU as both programs (0.88 s on the Xeon, 2.42 s on the Graviton2),
so no program can be 5% ahead without caching the matrix (~240 MB) or computing `1/d` another way, which the
problem does not allow. xisto is ahead by 1–2% on both.

**mandelbrot on the Graviton2**, a tie until `e897e76`, is now a win (6% ahead). Two changes:
- `mandelbrot-8` runs twelve pixels at a time after a byte with a pixel inside the set, and writes `2·zr·zi` as
  `p + p` with `p = zr·zi`. Doubling is exact, so the doubles are the same, and the chain is an addition shorter
  than a multiplication.
- The compiler's lane planner (`src/emit.c`) picks the split that puts the most work in vectors: twelve copies
  become three vectors of four instead of one of eight and four scalars. The C of every other program is unchanged.

## The xisto programs

Each name links to its source on GitHub; the original Node programs are in [`bench/benchmarksgame/node/`](https://github.com/xistolang/xisto/tree/main/bench/benchmarksgame/node).

| problem | xisto variant | what it does |
|---|---|---|
| spectral-norm | [`spectralnorm`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/spectralnorm.ts) (Xeon), [`spectralnorm-6`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/spectralnorm-6.ts), [`spectralnorm-7`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/spectralnorm-7.ts) | the products split in equal shares of rows across threads; the denominator of A carried along each row by additions; #7 joins the 40 products through `Atomics` counters instead of messages. Same divisions, same order, same doubles as the plain program |
| n-body | [`nbody`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/nbody.ts) | the plain program with types |
| fannkuch-redux | [`fannkuchredux-5`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/fannkuchredux-5.ts) | 720 ranges to a pool of workers; the permutation as bytes in vector registers (a flip is one shuffle); two permutations' flip chains counted in the same loop |
| binary-trees | [`binarytrees`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/binarytrees.ts), [`binarytrees-2`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/binarytrees-2.ts), [`binarytrees-3`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/binarytrees-3.ts) | trees built in parallel (`xisto:parallel` or a worker pool); nodes of 8 bytes (32-bit references), a statement's temporary trees freed right after it |
| mandelbrot | [`mandelbrot-6`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/mandelbrot-6.ts) (Xeon), [`mandelbrot-8`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/mandelbrot-8.ts) (Graviton2) | chunks of rows to a pool of workers; 16 (#6) or 8 and 12 (#8) pixels per call as SIMD vectors; rows written out as they finish |
| fasta | [`fasta`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/fasta.ts) | the plain program with types; the random sequences from `xisto:random` |
| k-nucleotide | [`knucleotide-7`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/knucleotide-7.ts) | the sequence at two bits per nucleotide, packed in parallel; small frames counted in arrays indexed by key, the big ones in partitions |
| reverse-complement | [`revcomp-10`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/revcomp-10.ts) | each sequence read backwards from the file one block at a time, blocks made by workers into a shared ring and written in order (10 MB); the newlines moved to their output places before the reverse, so each line is one run |
| regex-redux | [`regexredux-2`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/regexredux-2.ts) | the nine counts in a worker while the main thread does the replacements; the 50 MB string shared with the worker, not copied |
| pidigits | [`pidigits`](https://github.com/xistolang/xisto/blob/main/bench/benchmarksgame/pidigits.ts) | the plain program with types (`BigInt` on xisto's GMP-backed integers) |

## What changed since 10 Nov

The 10 Nov round had 7 wins, 3 ties on the Xeon and 8 wins, 1 tie, 1 loss (mandelbrot) on the Graviton2, with xisto
using more memory than Rust on binary-trees, fasta, k-nucleotide and reverse-complement. Between 10 and 13 Nov:

- **Compiler and runtime**, for every program: 32-bit references for classes that only point to themselves (the
  binary-trees node goes from 16 to 8 bytes); objects that cannot outlive one statement go to a region given back
  to the system after it; the order of SIMD lanes chosen per architecture; a 1 MB stdout pipe on Linux; large strings
  sent to a worker without a copy; `Int32Array` and `Atomics` (`wait`, `notify`) as in Node.
- **New variants**: `mandelbrot-6`/`-7` (lane copies, output written as rows finish), `fannkuchredux-5` (two
  permutations' flips in one loop), `knucleotide-7` (2 bits per base), `revcomp-9` (each sequence read back to
  front, one block at a time), `regexredux-2` (counts in a worker), `spectralnorm-7` (an `Atomics` barrier),
  `mandelbrot-8` (twelve pixels per run, with the lane planner above), `revcomp-10` (one run per line).

## Result files

- `bench/results/vm-2026-11-13/rust-ubuntu-xeon-2026-11-13-4a2f388.txt`
- `bench/results/vm-2026-11-13/rust-arm-graviton2-2026-11-13-4a2f388.txt`
- Earlier rounds of the same day (`*-937cb49`, `*-4a10af3`, `*-5296927`, `*-e5fb066`, `*-efd0fef`, `*-e897e76`) show
  each step of the work above.
