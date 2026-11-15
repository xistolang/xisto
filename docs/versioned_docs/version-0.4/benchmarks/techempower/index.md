---
title: TechEmpower
sidebar_position: 4
---

import {BestBars, LevelLines, CostBars} from '@site/src/components/Charts/TechEmpower';

# TechEmpower: HTTP servers

15 Nov 2026, main `f766bf8`. The [TechEmpower Framework Benchmarks](https://www.techempower.com/benchmarks/) (TFB)
measure web frameworks on fixed tests; this page runs its two tests without a database, **json** and **plaintext**,
with the TFB's rules, on the two AWS VMs. The database tests come next.

Head to head, with every level, latency, CPU and memory: [vs Rust](./vs-rust.md) (actix-web),
[vs Go](./vs-go.md) (net/http), [vs Bun](./vs-bun.md) and [vs Node](./vs-node.md) (http and fastify).

Each runtime is here once, with its plain HTTP server. **Running NestJS?** The same Nest app on Xisto, Node and Bun is
on [NestJS](../nest/index.md).

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
  [bench/results/techempower-2026-11-15-f766bf8](https://github.com/xistolang/xisto/tree/main/bench/results/techempower-2026-11-15-f766bf8), with every wrk run's own output in `raw/`.

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
| Xisto (native `Server`) | **71,666** | 256 | 66.9 ms | 25.4 | 5.5 MB |
| actix-web (Rust) | 64,161 | 128 | 20.0 ms | 29.9 | 14.4 MB |
| Bun | 54,474 | 128 | 13.7 ms | 35.8 | 88.4 MB |
| Go net/http | 33,141 | 64 | 5.5 ms | 58.1 | 30.7 MB |
| Node http | 29,258 | 64 | 6.9 ms | 67.0 | 290.3 MB |
| fastify (Node) | 23,017 | 128 | 14.0 ms | 85.6 | 301.6 MB |

Requests per second at each concurrency level (connections):

| server | 16 | 32 | 64 | 128 | 256 | 512 |
|---|---:|---:|---:|---:|---:|---:|
| Xisto (native `Server`) | 29,615 | 51,415 | 66,449 | 69,712 | 71,666 | 71,553 |
| actix-web (Rust) | 29,207 | 48,964 | 59,571 | 64,161 | 64,109 | 62,197 |
| Bun | 28,175 | 43,166 | 52,899 | 54,474 | 54,147 | 53,963 |
| Go net/http | 24,256 | 32,397 | 33,141 | 32,171 | 32,225 | 31,517 |
| Node http | 22,678 | 28,942 | 29,258 | 29,043 | 28,892 | 27,738 |
| fastify (Node) | 20,061 | 22,241 | 21,420 | 23,017 | 22,628 | 21,756 |

### plaintext

<BestBars test="plaintext" />

<LevelLines test="plaintext" />

<CostBars test="plaintext" metric="cpu" />

<CostBars test="plaintext" metric="rss" />

<LevelLines test="plaintext" metric="p99" />

#### plaintext in numbers

| server | best req/s | connections there | p99 there | CPU ms per 1k requests | peak RSS (all levels) |
|---|---:|---:|---:|---:|---:|
| Xisto (native `Server`) | **649,581** | 256 | 203.8 ms | 3.0 | 42.4 MB |
| actix-web (Rust) | 569,047 | 256 | 116.3 ms | 3.5 | 573.6 MB |
| Bun | 113,242 | 4,096 | 1.46 s | 17.4 | 95.6 MB |
| Node http | 51,714 | 4,096 | 1.03 s | 39.3 | 826.6 MB |
| Go net/http | 44,637 | 4,096 | 1.57 s | 44.5 | 567.1 MB |
| fastify (Node) | 36,534 | 4,096 | 1.03 s | 57.1 | 848.2 MB |

Requests per second at each concurrency level (connections; HTTP pipelining 16 deep):

| server | 256 | 1,024 | 4,096 | 16,384 |
|---|---:|---:|---:|---:|
| Xisto (native `Server`) | 649,581 | 596,731 | 552,728 | 411,863 |
| actix-web (Rust) | 569,047 | 522,104 | 473,669 | 328,127 |
| Bun | 111,901 | 110,828 | 113,242 | 86,769 |
| Node http | 51,086 | 50,687 | 51,714 | 35,409 |
| Go net/http | 41,014 | 44,095 | 44,637 | 33,867 |
| fastify (Node) | 34,576 | 35,116 | 36,534 | 24,701 |

## What this says

- **Xisto's native server is first on both tests**: 71,666 json requests per second against actix-web's 64,161
  (+12%), and 649,581 plaintext against 569,047 (+14%), with less CPU per request than any other server and the
  least memory of all (5.5 MB against 14.4 MB on json, 42.4 against 573.6 MB on plaintext).
- **At 16,384 connections** every server loses throughput, and Xisto still answers the most requests (411,863
  against 328,127). Its **tail latency is worse** than actix-web's at high concurrency, though, with a lower median:

  | plaintext | Xisto p50 | actix p50 | Xisto p90 | actix p90 | Xisto p99 | actix p99 |
  |---|---:|---:|---:|---:|---:|---:|
  | 4,096 connections | 64–70 ms | 74–79 ms | 262–348 ms | 203–222 ms | 0.59–0.92 s | 0.29 s |
  | 16,384 connections | 265–289 ms | 337–347 ms | 1.36–1.54 s | 1.04–1.16 s | 1.9–5.1 s | 1.5–3.4 s |

  The ranges are the three runs of each level, read from wrk's own output (`raw/`). At 16,384 connections the p99
  moves by seconds from run to run on both servers: it is set by the requests that hit wrk's 8 s timeout (Xisto 386,
  0 and 344 per run; actix-web 196, 0 and 162). The likely cause on Xisto's side is how a worker accepts new
  connections while thousands are busy; a fix is being measured.
- **p99 values marked —**: wrk 4.2 prints some percentiles as `0.00us` under pipelining (its correction for
  coordinated omission can add samples below the lowest one it recorded, and its percentile walk starts there). Those
  runs have no p99; a row's p99 is the median of the runs that have one, and — when none has.

## What changed since the first round

The first round (main `40cdc16`, same day) had actix-web 16% ahead on json and 1.7× on plaintext, and Xisto's
servers collapsing at 16,384 connections. The runtime
then stopped spending system calls a request does not need: three clock reads per request (on this VM's Xen
clocksource each is a real system call), an extra `read` that only returned `EAGAIN`, and the `Date` header
rendered once a second instead of per request; each worker got its own listening socket, so the kernel spreads new
connections; and the request head is parsed in one pass. On one core the cost went from 10.2 to 7.3 µs and from 6.0
to 2.7 system calls per request. The A/B rounds are in
[bench/results/techempower-2026-11-14/ab-te-syscalls](https://github.com/xistolang/xisto/tree/main/bench/results/techempower-2026-11-14/ab-te-syscalls).
