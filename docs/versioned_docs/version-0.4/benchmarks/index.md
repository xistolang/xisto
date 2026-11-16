---
title: Benchmarks
sidebar_position: 1
---

# Benchmarks

These are the headline results. Each table names its machine, its date and the result file in `bench/results/`.
How they were measured is in [Methodology](../performance/methodology.md).

## Read this first

- The Benchmarks Game against Rust was re-run on 13 Nov 2026 (main `4a2f388`), against Node and Bun the same day
  (main `e5fb066`), PolyBench against Rust and Go on 14 Nov (main `40cdc16`), TechEmpower and NestJS on 15 Nov (main `f766bf8`). The other tables are from 5–10 Nov.
- Some rows are lost or tied, and they are shown, not dropped: Bun on a slow dependency at 1 and 5 ms (4–5%), energy
  per HTTP request, streaming energy and SQLite time; Bun on Conduit's two cached routes and Node on its cached article
  by slug; actix-web on tail latency (p99) at high concurrency. The HTTP tables were re-measured on 14 Nov.
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

15 Nov 2026, main `f766bf8`, the TFB's rules: servers on the Xeon, wrk on the Graviton2 over the private network,
15 s per level, median of 3. Best requests per second:

| server | json | plaintext | json peak RSS |
|---|---:|---:|---:|
| Xisto (native) | **71,666** | **649,581** | 5.5 MB |
| actix-web (Rust) | 64,161 | 569,047 | 14.4 MB |
| Bun | 54,474 | 113,242 | 88.4 MB |
| Go net/http | 33,141 | 44,637 | 30.7 MB |
| Node http | 29,258 | 51,714 | 290.3 MB |
| fastify | 23,017 | 36,534 | 301.6 MB |

Xisto's native server is first on both tests, ahead of actix-web. At high concurrency Xisto's tail latency (p99) is
still worse than actix-web's, with a lower median. The per-level tables, charts and the latency distribution are on
[TechEmpower](./techempower/index.md).

## NestJS

The same Nest app, unchanged, built with Xisto or run on Node and Bun. On the TechEmpower routes (Xeon VM, 15 Nov)
the app built with Xisto serves 54,049 json requests per second in two processes with 16.2 MB, against Bun's 27,306
with 233 MB and Node's 7,027 with 354 MB. On Conduit, the RealWorld app with Postgres and Redis (M4, 15 Nov), Xisto
starts in 26 ms against Bun's 214 and Node's 264, holds 11.6 MB after the load against 130.8 and 154.4 MB, and
answers the most requests on five of seven routes; on the two served from the Redis cache Bun is first. The charts
and tables are on [NestJS](./nest/index.md).

## Benchmarks Game against Node and Bun

13 Nov 2026, the xisto variants of main `e5fb066` against the official Node programs under Node v26.12.0 and
Bun 1.4.4, median of 3, on the Apple M4, the Xeon and the Graviton2. xisto wins every row on all three machines, on
time and on memory: from 1.3× (regex-redux against Bun on the M4) to 200× (k-nucleotide against Node on the M4).
Most xisto variants use every core while the Node programs use one, so the CPU columns are the fairer comparison;
xisto uses less CPU on every row but regex-redux against Bun on the M4 (0.37 s against 0.34 s). The tables are on
[Xisto vs Node](./benchmarks-game/vs-node.md) and [Xisto vs Bun](./benchmarks-game/vs-bun.md).

## Benchmarks Game against Go

15 Nov 2026, main `f766bf8` against every official Go program (Go 1.27.2, regex-redux with PCRE 8.45 and its JIT)
on the Xeon: 10 wins out of 10, from 1.2× (reverse-complement) to 10.8× (binary-trees), with less CPU on every row
and less memory on 9 of 10 (n-body 2.0 against 1.9 MB). The table is on [Xisto vs Go](./benchmarks-game/vs-go.md).

## Servers

| | xisto | Node | Bun | source |
|---|---:|---:|---:|---|
| hello `/ping`, req/s | 244,918 | 127,738 | 183,577 | M4, 14 Nov, `2026-11-14-Apple-M4-http.csv` |
| hello `/ping`, RSS after load | 6.39 MB | 78.14 MB | 35.56 MB | same |
| Todo API list, req/s | 180,209 | 102,813 | 145,815 | M4, 14 Nov, `2026-11-14-Apple-M4-todo.csv` |
| Conduit (NestJS) startup | 26 ms | 264 ms | 214 ms | M4, 15 Nov, `conduit-mac-2026-11-15.txt` |
| Conduit RSS at rest | 7.5 MB | 142.5 MB | 76.5 MB | same |

## What you ship

| | xisto | Node | Bun |
|---|---:|---:|---:|
| `fib` program | 69,288 B | 61,220,224 B | 88,660,864 B |

For Node and Bun the size is the runtime binary and its private libraries, which must be installed to run the
program. Apple M4, 7 Nov 2026, `bench/results/2026-11-07-Apple-M4.csv`.

## Pages

- [Benchmarks Game](./benchmarks-game/index.md): against Rust, Node and Bun, per problem.
- [PolyBench](./polybench/index.md): 30 numerical kernels against Rust and Go, per kernel.
- [TechEmpower](./techempower/index.md): json and plaintext against actix, Go, Node, fastify and Bun.
- [NestJS](./nest/index.md): the same Nest app on Xisto, Node and Bun: the TechEmpower routes and Conduit.
- [HTTP](./http.md): hello, Todo API, threads, resilience, slow dependencies.
- [Energy, SQLite, streams and micro programs](./energy-sqlite-streams.md).

## See also

- [Methodology](../performance/methodology.md)
- [The performance model](../performance/model.md)
