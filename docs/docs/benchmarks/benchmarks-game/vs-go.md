---
title: Xisto vs Go
sidebar_position: 4
---

import {SpeedupBars, MemoryBars} from '@site/src/components/Charts/Speedup';

# Xisto vs Go

15 Nov 2026, main `f766bf8`, on the Xeon. Go is a compiled language with a garbage collector and goroutines, the
closest in spirit to what xisto does with TypeScript: native code, automatic memory, threads without ceremony.

## Setup

- **Go programs**: every Go program of the official source package (BSD-3), not only the fastest, built as the site
  builds them by `bench/benchmarksgame/go-build.sh`: plain `go build` in one module with the third-party packages
  the programs import (`github.com/ncw/gmp` for pidigits, two PCRE bindings for regex-redux). Go 1.27.2,
  `GOAMD64=v3` (the Xeon has AVX2, the same role as Rust's `target-cpu=native`). All 48 programs built.
- **PCRE**: regex-redux #4 and #5 bind PCRE 1 through cgo, and Ubuntu 26.04 no longer ships it (`libpcre3-dev` is
  gone; only PCRE 2 is packaged). PCRE 8.45, its last release, was built from source (SHA-256 checked) with JIT into
  a local prefix and passed to cgo through `CGO_CFLAGS`, `CGO_LDFLAGS` and `PKG_CONFIG_PATH`.
- **xisto**: every variant in `bench/benchmarksgame/`, built with `XISTO_CPU=native`; `spectralnorm-8` is measured
  but not scored, as on [Xisto vs Rust](./vs-rust.md#setup).
- **Command**: `python3 bench/benchmarksgame/langs.py 3 go=~/gobg/bin`, scored with
  `python3 bench/placar.py RESULTS go` (see [How every run is measured](./index.md#how-every-run-is-measured)).
  The six problems without an input file ran first; the four that read the fasta output ran in a second round of
  the same build (`*-run2.txt`), and regex-redux again once PCRE was built (`*-pcre.txt`).
- **Steal** during the first round: +219.

Go's programs use both cores too (goroutines), so time and CPU tell the same story here.

## Xeon E5-2686 v4 (AWS t2.medium, 2 vCPUs, AVX2)

Source: `bench/results/bg-go-2026-11-15/go-ubuntu-xeon-2026-11-15-f766bf8.txt` and `…-f766bf8-run2.txt`.

<SpeedupBars page="bg-vs-go" section="Xeon E5-2686 v4" unit="problem" />

<MemoryBars page="bg-vs-go" section="Xeon E5-2686 v4" unit="problem" />

| problem | xisto | Go | xisto time | Go time | xisto CPU | Go CPU | xisto mem | Go mem | result |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| binary-trees | binarytrees-3 | binarytrees_5 † | 1.538 s | 16.604 s | 2.99 s | 32.92 s | 114.3 MB | 307.2 MB | win |
| mandelbrot | mandelbrot-6 | mandelbrot | 1.323 s | 7.155 s | 2.51 s | 14.03 s | 7.3 MB | 33.2 MB | win |
| regex-redux | regexredux-2 | regexredux_5 | 0.680 s | 3.582 s | 0.97 s | 5.27 s | 149.1 MB | 312.7 MB | win |
| k-nucleotide | knucleotide-7 † | knucleotide_6 | 1.357 s | 6.973 s | 2.65 s | 12.28 s | 70.4 MB | 142.2 MB | win |
| fasta | fasta | fasta_2 | 1.135 s | 3.021 s | 0.36 s | 4.44 s | 2.4 MB | 10.3 MB | win |
| fannkuch-redux | fannkuchredux-5 | fannkuchredux_3 | 6.815 s | 17.082 s | 13.49 s | 34.07 s | 2.4 MB | 5.2 MB | win |
| spectral-norm | spectralnorm-7 | spectralnorm_2 † | 0.453 s | 1.124 s | 0.90 s | 2.17 s | 2.3 MB | 2.4 MB | win |
| n-body | nbody | nbody_8 | 2.451 s | 3.641 s | 2.45 s | 3.65 s | 2.0 MB | 1.9 MB | win |
| pidigits | pidigits | pidigits_4 | 0.628 s | 0.899 s | 0.63 s | 0.97 s | 2.6 MB | 5.0 MB | win |
| reverse-complement | revcomp-10 | revcomp | 0.838 s | 1.021 s | 0.24 s | 0.51 s | 11.0 MB | 245.1 MB | win |

10 wins out of 10, from 1.2× (reverse-complement) to 10.8× (binary-trees), with less CPU on every row. xisto's peak
memory is below Go's on 9 of 10; on n-body Go uses 1.9 MB against 2.0 MB. † Chosen for memory (up
to 2% slower with at least 10% less memory, or up to 5% slower with at least 25% less).

The widest gaps are where Go's runtime does the work: binary-trees allocates hundreds of millions of short-lived
nodes, which Go's collector traces while xisto frees a statement's temporary trees right after it in 8-byte nodes;
mandelbrot and fannkuch-redux are SIMD in xisto (vector types in the generated C) and scalar in Go, whose compiler
does not vectorize. Where both are plain scalar loops (n-body, pidigits on GMP) the gap is 1.4–1.5×.

## regex-redux

Go's fastest regex-redux programs call PCRE through cgo: #5 with PCRE's JIT takes 3.58 s, #4 17.5 s, and the two
that use Go's own `regexp` package (which guarantees linear time) about 27.7 s. xisto's `regexredux-2` takes 0.68 s,
with the nine counts in a worker while the main thread does the replacements.

## The xisto programs

The variants and what each does are listed on [Xisto vs Rust](./vs-rust.md#the-xisto-programs).

## Result files

- `bench/results/bg-go-2026-11-15/go-ubuntu-xeon-2026-11-15-f766bf8.txt` (the six problems without input)
- `bench/results/bg-go-2026-11-15/go-ubuntu-xeon-2026-11-15-f766bf8-run2.txt` (k-nucleotide, reverse-complement,
  regex-redux without PCRE, pidigits)
- `bench/results/bg-go-2026-11-15/go-ubuntu-xeon-2026-11-15-f766bf8-pcre.txt` (regex-redux with PCRE 8.45)
- `bench/results/bg-go-2026-11-15/go-build-ubuntu-xeon-2026-11-15.txt` (the build log)
