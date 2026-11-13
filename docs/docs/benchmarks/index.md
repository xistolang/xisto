---
title: Benchmarks
sidebar_position: 1
---

# Benchmarks

These are the headline results. Each table names its machine, its date and the result file in `bench/results/`.
How they were measured is in [Methodology](../performance/methodology.md).

## Read this first

- The numbers are from 5–10 Nov 2026. The compiler changed on 10–12 Nov (`any`, real promises, the backtracking
  regex engine). Those three changes were checked to leave the generated C of the benchmark programs identical,
  but a full re-run after all of the 10–12 Nov work is pending.
- Bun wins or ties some rows: the Todo API, energy per HTTP request, streaming time, SQLite time and regex-redux on
  the M4. Those rows are shown, not dropped.
- The M4 Benchmarks Game results (7 Nov) predate the parallel ports: every xisto program there is
  single-threaded. The VM results (9–10 Nov) use the parallel versions.
- Rust is ahead on mandelbrot on both VMs and on spectral-norm on the Graviton2, and xisto's binary-trees uses more
  memory than Rust's.

## Benchmarks Game against Rust

Fastest matching variant on each side, median of 3, 10 Nov 2026. Sources:
`bench/results/vm-2026-11-07/rust-ubuntu-xeon-2026-11-10t.txt`,
`bench/results/vm-2026-11-07/rust-arm-graviton2-2026-11-10t.txt`, scored in
`bench/results/placar-final-2026-11-10.md`.

| machine | wins | ties (within 5%) | losses |
|---|---:|---:|---|
| Xeon E5-2686 v4 (2 vCPUs) | 7 | 3 | none |
| Graviton2 (2 vCPUs) | 8 | 1 | mandelbrot, 1.08× |

The per-problem table is on [Benchmarks Game](./benchmarks-game.md).

## Benchmarks Game against Node and Bun

Apple M4, 7 Nov 2026, median of 3, Node v26.12.0 and Bun 1.4.4, xisto single-threaded
(`bench/results/benchmarksgame-2026-11-07-mac-5.txt`). xisto is faster than the better of Node and Bun on 9 of 10
problems, by 1.12× (pidigits) to 2.57× (reverse-complement). On regex-redux Bun is 1% faster (0.354 s against 0.358 s).

## Servers

| | xisto | Node | Bun | source |
|---|---:|---:|---:|---|
| hello `/ping`, req/s | 231,097 | 136,913 | 198,223 | M4, 7 Nov, `2026-11-07-Apple-M4-http.csv` |
| hello `/ping`, RSS after load | 4.20 MB | 100.72 MB | 34.47 MB | same |
| Todo API list, req/s | 111,914 | 68,697 | 115,425 | M4, 5 Nov, `2026-11-05-Apple-M4-todo.csv` |
| Conduit (NestJS) startup | 21 ms | 268 ms | — | M4, 8 Nov, `conduit-mac-2026-11-08.txt` |
| Conduit RSS at rest | 2.6 MB | 136.0 MB | — | same |

## What you ship

| | xisto | Node | Bun |
|---|---:|---:|---:|
| `fib` program | 69,288 B | 61,220,224 B | 88,660,864 B |

For Node and Bun the size is the runtime binary and its private libraries, which must be installed to run the
program. Apple M4, 7 Nov 2026, `bench/results/2026-11-07-Apple-M4.csv`.

## Pages

- [Benchmarks Game](./benchmarks-game.md): against Rust, Node and Bun, per problem.
- [HTTP](./http.md): hello, Todo API, the Conduit NestJS app, threads, resilience, slow dependencies.
- [Energy, SQLite, streams and micro programs](./energy-sqlite-streams.md).

## See also

- [Methodology](../performance/methodology.md)
- [The performance model](../performance/model.md)
