---
title: TechEmpower
sidebar_position: 3
---

# TechEmpower: HTTP servers

14 Nov 2026. The [TechEmpower Framework Benchmarks](https://www.techempower.com/benchmarks/) (TFB) measure web
frameworks on fixed tests; this page runs its two tests without a database, **json** and **plaintext**, with the
TFB's rules, on the two AWS VMs. The database tests come next.

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
- **Xisto**: the native `Server` API with 2 workers, and a NestJS app with the TFB's routes
  (`bench/techempower/xisto/nest`) built with Xisto in three modes:
  - **workers with the lock** (`XISTO_WORKERS=2`, the default): one event loop per CPU, the handlers one at a time
    under a lock, because Nest's singletons are shared;
  - **workers without the lock** (`XISTO_LOCK=0`): the handlers in parallel;
  - **2 processes on one port** (`XISTO_REUSEPORT=1`, one worker each): the kernel spreads the connections, each
    process has its own singletons, nothing is shared, as Node's cluster or Bun's `reusePort`.
- **CPU** is the server's user + system time per 1,000 requests; **RSS** the peak of all its processes.
- Source: `bench/techempower/` (apps, `run.py`, `validate.py`), raw results in
  [bench/results/techempower-2026-11-14](https://github.com/xistolang/xisto/tree/main/bench/results/techempower-2026-11-14).

## Results

### json

| server | best req/s | p99 there | CPU ms per 1k requests | peak RSS |
|---|---:|---:|---:|---:|
| actix-web (Rust) | **65,068** | 16.9 ms | 29.8 | 12.3 MB |
| Xisto (native `Server`) | 56,352 | 16.4 ms | 33.9 | 5.5 MB |
| Bun | 55,207 | 11.8 ms | 35.2 | 89.9 MB |
| Xisto + NestJS, 2 processes on one port | 48,332 | 10.8 ms | 40.5 | 16.0 MB |
| Xisto + NestJS, workers with the lock | 38,146 | 17.1 ms | 42.6 | 10.9 MB |
| Go net/http | 32,831 | 5.8 ms | 58.4 | 20.1 MB |
| Node http | 29,524 | 6.1 ms | 66.4 | 243.8 MB |
| fastify (Node) | 22,962 | 7.1 ms | 85.8 | 263.3 MB |
| Xisto + NestJS, workers without the lock | crashed under load | | | |

Requests per second at each concurrency level (connections):

| server | 16 | 32 | 64 | 128 | 256 | 512 |
|---|---:|---:|---:|---:|---:|---:|
| Xisto (native `Server`) | 28,560 | 41,425 | 50,202 | 56,237 | 56,352 | 56,295 |
| Xisto + NestJS, 2 processes on one port | 27,703 | 39,803 | 48,046 | 48,332 | 47,075 | 47,670 |
| Xisto + NestJS, workers with the lock | 26,284 | 32,533 | 33,892 | 36,941 | 38,146 | 37,750 |
| Xisto + NestJS, workers without the lock | crashed |  |  |  |  |  |
| actix-web (Rust) | 29,152 | 49,887 | 62,310 | 65,068 | 64,408 | 63,786 |
| Bun | 28,628 | 41,187 | 53,128 | 55,207 | 54,948 | 54,384 |
| Go net/http | 24,671 | 32,603 | 32,831 | 32,442 | 32,554 | 31,765 |
| Node http | 22,685 | 27,363 | 29,524 | 29,519 | 29,054 | 27,452 |
| fastify (Node) | 18,602 | 22,799 | 22,962 | 22,684 | 22,079 | 21,600 |

### plaintext

| server | best req/s | p99 there | CPU ms per 1k requests | peak RSS |
|---|---:|---:|---:|---:|
| actix-web (Rust) | **569,578** | — | 3.5 | 325.4 MB |
| Xisto (native `Server`) | 331,283 | 16.7 ms | 6.0 | 41.9 MB |
| Xisto + NestJS, 2 processes on one port | 199,633 | — | 9.9 | 74.3 MB |
| Bun | 114,082 | 1380.0 ms | 17.4 | 94.4 MB |
| Xisto + NestJS, workers with the lock | 97,339 | 48.6 ms | 14.0 | 69.1 MB |
| Node http | 51,486 | 4080.0 ms | 39.1 | 609.4 MB |
| Go net/http | 44,851 | 1540.0 ms | 44.3 | 178.3 MB |
| fastify (Node) | 35,972 | 1550.0 ms | 58.3 | 728.9 MB |
| Xisto + NestJS, workers without the lock | crashed under load | | | |

Requests per second at each concurrency level (connections; HTTP pipelining 16 deep):

| server | 256 | 1024 | 4096 | 16384 |
|---|---:|---:|---:|---:|
| Xisto (native `Server`) | 331,283 | 302,379 | 278,373 | 90,153 |
| Xisto + NestJS, 2 processes on one port | 199,633 | 193,004 | 179,739 | 29,311 |
| Xisto + NestJS, workers with the lock | 97,339 | 94,755 | 92,809 | 217 |
| Xisto + NestJS, workers without the lock | crashed |  |  |  |
| actix-web (Rust) | 569,578 | 528,068 | 478,056 | 329,444 |
| Bun | 113,731 | 113,997 | 114,082 | 89,059 |
| Go net/http | 41,543 | 44,209 | 44,851 | 32,751 |
| Node http | 51,439 | 51,486 | 51,171 | 34,508 |
| fastify (Node) | 33,618 | 35,207 | 35,972 | 22,978 |
## What this says

- **actix-web is ahead**: 16% on json and 1.7× on plaintext. Xisto's native server is second on both, ahead of
  Bun, Go and Node, with the least memory of all on json (5.5 MB).
- **NestJS built with Xisto** in two processes serves 48,332 json requests per second: more than Go's net/http,
  Node's own http module and fastify, and 1.6× what Node's http does, with 16 MB.
- **Nest without the lock crashes** under load: its singletons are shared by the workers and touched without
  synchronization. The lock is the default for that reason; the multi-process mode is the one that scales.
- **16,384 connections are Xisto's weak point**: on plaintext the native server falls from 278,373 to 90,153
  requests per second, the Nest app with the lock to 217, the multi-process one to 29,311, while the references
  lose 36% at most (fastify). Under investigation.
- p99 values marked — were not read from wrk's output for that run.
