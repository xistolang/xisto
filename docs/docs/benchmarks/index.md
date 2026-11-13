---
title: Benchmarks
sidebar_position: 1
---

# Benchmarks

These are the headline results. Each table names its machine, its date and the result file in `bench/results/`.
How they were measured is in [Methodology](../performance/methodology.md).

## Read this first

- The Benchmarks Game against Rust was re-run on 13 Nov 2026 (main `e897e76`), against Node and Bun the same day
  (main `e5fb066`). The other tables are from 5–10 Nov.
- Bun wins or ties some rows: the Todo API, energy per HTTP request, streaming time and SQLite time. Those rows are
  shown, not dropped.
- Against Rust no problem is lost on either VM. Two rows are ties within 5%: spectral-norm on both VMs, at the
  floor of the divisions the problem asks for. xisto's peak memory is at or below Rust's on every row but
  reverse-complement on the Graviton2 (188 MB against 123 MB).

## Benchmarks Game against Rust

Fastest matching variant on each side, median of 3, 13 Nov 2026, main `e897e76`, `XISTO_CPU=native`. Sources:
`bench/results/vm-2026-11-13/rust-ubuntu-xeon-2026-11-13-e897e76.txt` and
`bench/results/vm-2026-11-13/rust-arm-graviton2-2026-11-13-e897e76.txt`, scored by `bench/placar.py`.

| machine | wins | ties (within 5%) | losses | memory |
|---|---:|---:|---|---|
| Xeon E5-2686 v4 (2 vCPUs) | 9 | 1 (spectral-norm) | none | below Rust on all 10 |
| Graviton2 (2 vCPUs) | 9 | 1 (spectral-norm) | none | at or below Rust on 9; reverse-complement 188 MB against 123 MB |

The per-problem tables are on [Xisto vs Rust](./benchmarks-game/vs-rust.md).

## Benchmarks Game against Node and Bun

13 Nov 2026, the same xisto variants as the Rust round against the official Node programs under Node v26.12.0 and
Bun 1.4.4, median of 3, on the Apple M4, the Xeon and the Graviton2. xisto wins every row on all three machines, on
time and on memory: from 1.3× (regex-redux against Bun on the M4) to 200× (k-nucleotide against Node on the M4).
Most xisto variants use every core while the Node programs use one, so the CPU columns are the fairer comparison;
xisto uses less CPU on every row but regex-redux against Bun on the M4 (0.37 s against 0.34 s). The tables are on
[Xisto vs Node](./benchmarks-game/vs-node.md) and [Xisto vs Bun](./benchmarks-game/vs-bun.md).

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

- [Benchmarks Game](./benchmarks-game/index.md): against Rust, Node and Bun, per problem.
- [HTTP](./http.md): hello, Todo API, the Conduit NestJS app, threads, resilience, slow dependencies.
- [Energy, SQLite, streams and micro programs](./energy-sqlite-streams.md).

## See also

- [Methodology](../performance/methodology.md)
- [The performance model](../performance/model.md)
