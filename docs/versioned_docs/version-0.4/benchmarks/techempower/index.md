---
title: TechEmpower
sidebar_position: 4
---

import {BestBars, LevelLines, CostBars} from '@site/src/components/Charts/TechEmpower';

# TechEmpower: HTTP servers

14 Nov 2026, main `3f24503`. The [TechEmpower Framework Benchmarks](https://www.techempower.com/benchmarks/) (TFB)
measure web frameworks on fixed tests; this page runs its two tests without a database, **json** and **plaintext**,
with the TFB's rules, on the two AWS VMs. The database tests come next.

Head to head, with every level, latency, CPU and memory: [vs Rust](./vs-rust.md) (actix-web),
[vs Go](./vs-go.md) (net/http), [vs Bun](./vs-bun.md) and [vs Node](./vs-node.md) (http and fastify).

Each runtime is here once, with its plain HTTP server. **Running NestJS?** The same Nest app on Xisto, Node and Bun is
on [NestJS](../nest.md).

## Setup

- **Topology**: the servers on the Xeon E5-2686 v4 (2 vCPUs, 3.9 GB, Ubuntu); the load generator,
  [wrk](https://github.com/wg/wrk) 4.2.0, on the Graviton2 (2 vCPUs), over the VPC's private network. The
  generator never shares a CPU with the server.
- **TFB rules**: the TFB's levels (json: 16 to 512 connections; plaintext: 256 to 16,384 with HTTP pipelining 16
  deep, the TFB's `pipeline.lua`), 15 s per level, the median of 3 runs, every server checked first by a validator
  with the TFB's rules (headers, body, content type).
- **References**: the TFB's own implementations at commit `57d92fb`: actix-web (Rust 1.99, `target-cpu=native`),
  Go net/http (Go 1.27), Node http and fastify (Node v22.22.1, Ubuntu's), Bun 1.4.4 (`reusePort`, one process
  per CPU). Each uses every core, as the TFB runs them.
- **Xisto**: the native `Server` API (`bench/techempower/xisto/native`) with 2 workers, one event loop per CPU.
- **CPU** is the server's user + system time per 1,000 requests at the best level; **peak RSS** the largest of all
  its processes over every level.
- Source: `bench/techempower/` (apps, `run.py`, `validate.py`, `chartdata.py` for the charts), raw results in
  [bench/results/techempower-2026-11-14-3f24503](https://github.com/xistolang/xisto/tree/main/bench/results/techempower-2026-11-14-3f24503).

## Results

### json

<BestBars test="json" />

<LevelLines test="json" />

<CostBars test="json" metric="cpu" />

<CostBars test="json" metric="rss" />

<LevelLines test="json" metric="p99" />

#### json in numbers

| server | best req/s | connections there | p99 there | CPU ms per 1k requests | peak RSS (all levels) |
|---|---:|---:|---:|---:|---:|
| Xisto (native `Server`) | **71,544** | 128 | 31.5 ms | 26.1 | 5.5 MB |
| actix-web (Rust) | 65,891 | 256 | 45.6 ms | 28.1 | 14.1 MB |
| Bun | 54,497 | 256 | 22.0 ms | 35.8 | 90.8 MB |
| Go net/http | 32,636 | 64 | 6.1 ms | 58.8 | 30.8 MB |
| Node http | 29,317 | 64 | 6.3 ms | 67.1 | 289.4 MB |
| fastify (Node) | 25,672 | 128 | 12.9 ms | 76.7 | 303.7 MB |

Requests per second at each concurrency level (connections):

| server | 16 | 32 | 64 | 128 | 256 | 512 |
|---|---:|---:|---:|---:|---:|---:|
| Xisto (native `Server`) | 29,680 | 50,193 | 65,676 | 71,544 | 71,383 | 70,573 |
| actix-web (Rust) | 29,050 | 49,526 | 60,875 | 64,773 | 65,891 | 64,270 |
| Bun | 28,016 | 43,747 | 52,600 | 54,024 | 54,497 | 53,664 |
| Go net/http | 24,057 | 32,629 | 32,636 | 32,498 | 32,038 | 31,509 |
| Node http | 22,580 | 28,560 | 29,317 | 29,210 | 28,643 | 27,466 |
| fastify (Node) | 19,562 | 24,263 | 25,357 | 25,672 | 25,174 | 21,719 |

### plaintext

<BestBars test="plaintext" />

<LevelLines test="plaintext" />

<CostBars test="plaintext" metric="cpu" />

<CostBars test="plaintext" metric="rss" />

<LevelLines test="plaintext" metric="p99" />

#### plaintext in numbers

| server | best req/s | connections there | p99 there | CPU ms per 1k requests | peak RSS (all levels) |
|---|---:|---:|---:|---:|---:|
| Xisto (native `Server`) | **662,156** | 256 | — | 3.0 | 42.2 MB |
| actix-web (Rust) | 570,564 | 256 | — | 3.5 | 562.7 MB |
| Bun | 114,146 | 4,096 | 1550.0 ms | 17.3 | 96.9 MB |
| Node http | 51,066 | 4,096 | 961.5 ms | 40.0 | 607.1 MB |
| Go net/http | 44,657 | 1,024 | 451.0 ms | 44.4 | 540.6 MB |
| fastify (Node) | 36,387 | 4,096 | 1080.0 ms | 57.0 | 843.5 MB |

Requests per second at each concurrency level (connections; HTTP pipelining 16 deep):

| server | 256 | 1,024 | 4,096 | 16,384 |
|---|---:|---:|---:|---:|
| Xisto (native `Server`) | 662,156 | 598,816 | 572,353 | 392,360 |
| actix-web (Rust) | 570,564 | 523,963 | 476,486 | 324,000 |
| Bun | 113,941 | 113,461 | 114,146 | 86,961 |
| Go net/http | 41,237 | 44,657 | 44,631 | 33,274 |
| Node http | 50,118 | 50,556 | 51,066 | 35,627 |
| fastify (Node) | 33,828 | 35,410 | 36,387 | 24,318 |

## What this says

- **Xisto's native server is first on both tests**: 71,544 json requests per second against actix-web's 65,891
  (+9%), and 662,156 plaintext against 570,564 (+16%), with less CPU per request and the least memory of all on
  json (5.5 MB against 14.1 MB).
- **At 16,384 connections** every server loses throughput (Xisto's native 41%, actix-web 43%), and Xisto still
  answers the most requests (392,360 against 324,000). Its **tail latency is worse** than actix-web's at high
  concurrency, though: p99 1.5 s against 0.43 s at 4,096 connections and 3.4 s against 2.3 s at 16,384, with a
  lower median (68 ms against 74 ms, 277 ms against 338 ms). Under investigation: how the event loop shares its
  turns between connections that have many pipelined requests waiting.
- p99 values marked — were not read from wrk's output for that run.

## What changed since the first round

The first round (main `40cdc16`, same day) had actix-web 16% ahead on json and 1.7× on plaintext, and Xisto's
servers collapsing at 16,384 connections. The runtime
then stopped spending system calls a request does not need: three clock reads per request (on this VM's Xen
clocksource each is a real system call), an extra `read` that only returned `EAGAIN`, and the `Date` header
rendered once a second instead of per request; each worker got its own listening socket, so the kernel spreads new
connections; and the request head is parsed in one pass. On one core the cost went from 10.2 to 7.3 µs and from 6.0
to 2.7 system calls per request. The A/B rounds are in
[bench/results/techempower-2026-11-14/ab-te-syscalls](https://github.com/xistolang/xisto/tree/main/bench/results/techempower-2026-11-14/ab-te-syscalls).
