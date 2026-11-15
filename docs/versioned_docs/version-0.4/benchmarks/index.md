---
title: Benchmarks
sidebar_position: 1
---

# Benchmarks

These are the headline results. Each table names its machine, its date and the result file in `bench/results/`.
How they were measured is in [Methodology](../performance/methodology.md).

## Read this first

- The Benchmarks Game against Rust was re-run on 13 Nov 2026 (main `4a2f388`), against Node and Bun the same day
  (main `e5fb066`), PolyBench against Rust and Go on 14 Nov (main `40cdc16`), TechEmpower on 14 Nov. The other tables are from 5–10 Nov.
- Some rows are lost or tied, and they are shown, not dropped: Bun on a slow dependency at 1 and 5 ms (4–5%), energy
  per HTTP request, streaming energy and SQLite time; Node on Conduit's article by slug with the cache warm; actix-web
  on tail latency (p99) at high concurrency. The HTTP tables were re-measured on 14 Nov.
- Against Rust no Benchmarks Game problem is lost on either VM, and no PolyBench kernel against Rust or Go. Two rows are ties within 5%: spectral-norm on both VMs, at the
  floor of the divisions the problem asks for. xisto's peak memory is below Rust's on 9 of 10 rows on the Xeon (fannkuch-redux: 2.3 against
  2.2 MB) and on all 10 on the Graviton2.

## Benchmarks Game against Rust

Fastest matching variant on each side, median of 3, 13 Nov 2026, main `4a2f388`, `XISTO_CPU=native`. Sources:
`bench/results/vm-2026-11-13/rust-ubuntu-xeon-2026-11-13-4a2f388.txt` and
`bench/results/vm-2026-11-13/rust-arm-graviton2-2026-11-13-4a2f388.txt`, scored by `bench/placar.py`.

| machine | wins | ties (within 5%) | losses | memory |
|---|---:|---:|---|---|
| Xeon E5-2686 v4 (2 vCPUs) | 9 | 1 (spectral-norm) | none | below Rust on 9 of 10 (fannkuch-redux 2.3 against 2.2 MB) |
| Graviton2 (2 vCPUs) | 9 | 1 (spectral-norm) | none | below Rust on all 10 |

The per-problem tables are on [Xisto vs Rust](./benchmarks-game/vs-rust.md).

## PolyBench against Rust and Go

14 Nov 2026, main `40cdc16`. PolyBench/C 4.2.1, 30 numerical kernels on the LARGE dataset, ported with the same
loops in the same order; Rust is the community port polybench-rs, Go a line-by-line transliteration of the C. No
fused multiply-add on any side, and every side's output matches the C reference's bit for bit. Kernel time, median
of 3 (close kernels re-measured with the sides interleaved), a tie within 3%, against the faster of Rust and Go:

| machine | wins | ties (within 3%) | losses |
|---|---:|---:|---|
| Xeon E5-2686 v4 (2 vCPUs) | 21 | 9 | none |
| Graviton2 (2 vCPUs) | 21 | 9 | none |

The per-kernel tables, with the C reference beside them, are on [PolyBench](./polybench/index.md).

## TechEmpower (json, plaintext)

14 Nov 2026, main `3f24503`, the TFB's rules: servers on the Xeon, wrk on the Graviton2 over the private network,
15 s per level, median of 3. Best requests per second:

| server | json | plaintext | json peak RSS |
|---|---:|---:|---:|
| Xisto (native) | **71,544** | **662,156** | 5.5 MB |
| actix-web (Rust) | 65,891 | 570,564 | 14.1 MB |
| Bun | 54,497 | 114,146 | 90.8 MB |
| Xisto + NestJS, 2 processes | 53,032 | 239,187 | 16.2 MB |
| Xisto + NestJS, with the lock | 44,216 | 116,817 | 10.7 MB |
| Go net/http | 32,636 | 44,657 | 30.8 MB |
| Node http | 29,317 | 51,066 | 289.4 MB |
| fastify | 25,672 | 36,387 | 303.7 MB |

Xisto's native server is first on both tests, ahead of actix-web, and NestJS built with Xisto passes Go, Node and
fastify. NestJS's workers without the handler lock crash (shared singletons). At high concurrency Xisto's tail
latency (p99) is still worse than actix-web's. The per-level tables and charts are on [TechEmpower](./techempower.md).

## Benchmarks Game against Node and Bun

13 Nov 2026, the xisto variants of main `e5fb066` against the official Node programs under Node v26.12.0 and
Bun 1.4.4, median of 3, on the Apple M4, the Xeon and the Graviton2. xisto wins every row on all three machines, on
time and on memory: from 1.3× (regex-redux against Bun on the M4) to 200× (k-nucleotide against Node on the M4).
Most xisto variants use every core while the Node programs use one, so the CPU columns are the fairer comparison;
xisto uses less CPU on every row but regex-redux against Bun on the M4 (0.37 s against 0.34 s). The tables are on
[Xisto vs Node](./benchmarks-game/vs-node.md) and [Xisto vs Bun](./benchmarks-game/vs-bun.md).

## Servers

| | xisto | Node | Bun | source |
|---|---:|---:|---:|---|
| hello `/ping`, req/s | 244,918 | 127,738 | 183,577 | M4, 14 Nov, `2026-11-14-Apple-M4-http.csv` |
| hello `/ping`, RSS after load | 6.39 MB | 78.14 MB | 35.56 MB | same |
| Todo API list, req/s | 180,209 | 102,813 | 145,815 | M4, 14 Nov, `2026-11-14-Apple-M4-todo.csv` |
| Conduit (NestJS) startup | 20 ms | 267 ms | — | M4, 14 Nov, `conduit-mac-2026-11-14.txt` |
| Conduit RSS at rest | 7.3 MB | 137.3 MB | — | same |

## What you ship

| | xisto | Node | Bun |
|---|---:|---:|---:|
| `fib` program | 69,288 B | 61,220,224 B | 88,660,864 B |

For Node and Bun the size is the runtime binary and its private libraries, which must be installed to run the
program. Apple M4, 7 Nov 2026, `bench/results/2026-11-07-Apple-M4.csv`.

## Pages

- [Benchmarks Game](./benchmarks-game/index.md): against Rust, Node and Bun, per problem.
- [PolyBench](./polybench/index.md): 30 numerical kernels against Rust and Go, per kernel.
- [TechEmpower](./techempower.md): json and plaintext against actix, Go, Node, fastify and Bun.
- [HTTP](./http.md): hello, Todo API, the Conduit NestJS app, threads, resilience, slow dependencies.
- [Energy, SQLite, streams and micro programs](./energy-sqlite-streams.md).

## See also

- [Methodology](../performance/methodology.md)
- [The performance model](../performance/model.md)
