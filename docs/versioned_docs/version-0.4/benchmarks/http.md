---
title: HTTP
sidebar_position: 3
---

import {BarChart} from '@site/src/components/Charts';

# HTTP

Unless a section says otherwise, the HTTP results are from an Apple M4 (10 cores, 4 of them performance cores, macOS
15.5), measured on 15 Nov 2026 with Xisto 0.4.2 (main `6138390`), the load generator [`oha`](https://github.com/hatoo/oha)
1.16.0 on the same machine, Node v26.12.0, Bun 1.4.4, Rust 1.99.1 with [axum](https://github.com/tokio-rs/axum) 0.8.9 on
tokio 1.54.1, and Go 1.27.2 with the standard library's `net/http`. Each server is one process with one event loop
unless a table says otherwise: Rust on tokio's `current_thread` runtime, Go with `GOMAXPROCS=1`. The Rust and Go
servers ([bench/http/rust](https://github.com/xistolang/xisto/tree/main/bench/http/rust),
[bench/http/go](https://github.com/xistolang/xisto/tree/main/bench/http/go)) answer every measured route byte for
byte as the Xisto server does (status, content type, body), checked with `bench/http/probe.sh`. NestJS, the Conduit
app included, has [its own page](./nest/index.md). The first Linux results are in [Node's http module on Linux](#nodes-http-module-on-linux).

## Hello server

64 keep-alive connections, 10 s per route. RSS is sampled right after the load; startup is the time from spawn to
the first 200 on `/ping`. Source: `bench/results/2026-11-15-Apple-M4-http.csv`.

<BarChart
  title="GET /ping: requests per second (higher is better)"
  rows={[
    {name: 'Xisto', value: 251058, highlight: true, detail: [['p99', '0.441 ms'], ['RSS after load', '7.36 MB']]},
    {name: 'Bun', value: 200488, detail: [['p99', '0.582 ms'], ['RSS after load', '34.88 MB']]},
    {name: 'Rust (axum)', value: 200353, detail: [['p99', '0.403 ms'], ['RSS after load', '6.86 MB']]},
    {name: 'Go (net/http)', value: 150751, detail: [['p99', '0.724 ms'], ['RSS after load', '13.66 MB']]},
    {name: 'Node', value: 143885, detail: [['p99', '0.825 ms'], ['RSS after load', '111.45 MB']]},
  ]}
  caption="One process, one event loop each (Rust: tokio current_thread; Go: GOMAXPROCS=1); every value is in the table below."
/>

<BarChart
  title="RSS after the load, in MB (lower is better)"
  unit="MB"
  rows={[
    {name: 'Xisto', value: 7.36, highlight: true, detail: []},
    {name: 'Bun', value: 34.88, detail: []},
    {name: 'Rust (axum)', value: 6.86, detail: []},
    {name: 'Go (net/http)', value: 13.66, detail: []},
    {name: 'Node', value: 111.45, detail: []},
  ]}
  caption="Resident memory sampled right after the /ping load."
  lowerIsBetter
  format={(v) => v.toFixed(1)}
  formatFull={(v) => `${v.toFixed(2)} MB`}
/>

| server | route | req/s | p50 | p99 | RSS after load | startup | executable |
|---|---|---:|---:|---:|---:|---:|---:|
| Xisto | `/ping` | **251,058** | 0.218 ms | 0.441 ms | 7.36 MB | 1 ms | 130,048 B |
| Xisto | `/hello?name=isa` | **248,671** | 0.219 ms | 0.448 ms | 8.25 MB | | |
| Bun | `/ping` | 200,488 | 0.269 ms | 0.582 ms | 34.88 MB | 2 ms | 88,660,864 B |
| Bun | `/hello?name=isa` | 191,360 | 0.282 ms | 0.614 ms | 45.66 MB | | |
| Rust (axum) | `/ping` | 200,353 | 0.314 ms | 0.403 ms | 6.86 MB | 1 ms | 1,061,176 B |
| Rust (axum) | `/hello?name=isa` | 194,162 | 0.325 ms | 0.407 ms | 7.91 MB | | |
| Go (net/http) | `/ping` | 150,751 | 0.421 ms | 0.724 ms | 13.66 MB | 1 ms | 5,648,194 B |
| Go (net/http) | `/hello?name=isa` | 143,955 | 0.441 ms | 0.774 ms | 13.67 MB | | |
| Node | `/ping` | 143,885 | 0.380 ms | 0.825 ms | 111.45 MB | 14 ms | 61,220,224 B |
| Node | `/hello?name=isa` | 133,701 | 0.413 ms | 0.895 ms | 112.44 MB | | |

Xisto answers 25% more requests than Bun and Rust, which tie, and 67% more than Go; Rust has the lowest p99 and the
least memory, Xisto close behind. The same client program (`bench/http/client.ts` and its ports, 20,000 sequential
requests against the Xisto server) takes 0.55 s and peaks at 1.94 MB on Xisto, 0.79 s and 13.02 MB on Go, 1.01 s and
36.08 MB on Bun, 1.31 s and 2.94 MB on Rust (reqwest), 1.70 s and 218.09 MB on Node.

## Todo API

A JSON API seeded with 50 todos; 64 connections, 10 s per route. `list` serializes all 50 items, `read` returns
one, `update` parses a JSON body. Source: `bench/results/2026-11-15-Apple-M4-todo.csv`.

<BarChart
  title="Todo API, list (50 items as JSON): requests per second (higher is better)"
  rows={[
    {name: 'Xisto', value: 178355, highlight: true, detail: [['p99', '0.680 ms'], ['RSS', '8.38 MB']]},
    {name: 'Bun', value: 155674, detail: [['p99', '0.825 ms'], ['RSS', '34.19 MB']]},
    {name: 'Rust (axum)', value: 151901, detail: [['p99', '0.512 ms'], ['RSS', '6.77 MB']]},
    {name: 'Go (net/http)', value: 79875, detail: [['p99', '1.364 ms'], ['RSS', '14.09 MB']]},
    {name: 'Node', value: 103642, detail: [['p99', '1.220 ms'], ['RSS', '110.34 MB']]},
  ]}
  caption="One process, one event loop each; every value is in the table below."
/>

| route | Xisto | Bun | Rust (axum) | Go (net/http) | Node |
|---|---|---|---|---|---|
| list | **178,355 req/s**, p99 0.680 ms, 8.38 MB | 155,674 req/s, p99 0.825 ms, 34.19 MB | 151,901 req/s, p99 0.512 ms, 6.77 MB | 79,875 req/s, p99 1.364 ms, 14.09 MB | 103,642 req/s, p99 1.220 ms, 110.34 MB |
| read | **227,399 req/s**, p99 0.502 ms, 8.81 MB | 180,935 req/s, p99 0.659 ms, 41.45 MB | 176,708 req/s, p99 0.459 ms, 7.86 MB | 126,378 req/s, p99 0.909 ms, 14.12 MB | 121,582 req/s, p99 0.997 ms, 111.11 MB |
| update | **215,122 req/s**, p99 0.541 ms, 9.64 MB | 159,871 req/s, p99 0.763 ms, 45.50 MB | 163,697 req/s, p99 0.712 ms, 8.41 MB | 110,308 req/s, p99 1.053 ms, 14.14 MB | 101,882 req/s, p99 1.227 ms, 129.61 MB |

Xisto is ahead on all three routes: 15% to 35% over Rust and Bun, 1.7 to 2.2 times Go and Node. Rust has the lowest
p99 on `list` and `read` and the least memory (6.8 to 8.4 MB, Xisto 8.4 to 9.6 MB). Go's `encoding/json` makes `list`
its slowest route: 79,875 req/s, half of its `read`.

## Node's http module on Linux

`node:http` came to xisto with the Node core modules (main `dedc7b2`). The same `/ping` server written with Node's
`http.createServer` (`bench/http/server.node.ts`, unchanged) built with xisto, beside xisto's own `Server` API, on
the Xeon E5-2686 v4 (2 vCPUs, Ubuntu). wrk 4.1.0, 1 thread, 64 connections, 10 s, on the same machine, median of 3
interleaved rounds, one event loop. Source: `bench/results/http-2026-11-14/xeon-ab-node-compat.txt`.

| server (built with xisto) | route | req/s | RSS after load |
|---|---|---:|---:|
| xisto `Server` | `/ping` | 102,411 | 3.1 MB |
| xisto `Server` (Todo API) | `/todos` | 100,521 | 3.2 MB |
| Node's `http.createServer` | `/ping` | 50,231 | 12.0 MB |

Node's API costs half the throughput of xisto's own on this machine: each request builds an `IncomingMessage`
stream and a `ServerResponse` with their events, as Node does. The same run checked that the Node modules did not
slow the native server: main before the merge served 103,598 and 100,680 req/s.

## Threads

256 connections, 10 s per route, the load generator on the same machine. `XISTO_WORKERS=N` runs N event loops on
one port; Node runs a cluster, Bun `reusePort`, Rust tokio's multi-thread runtime with N workers, Go
`GOMAXPROCS=N`. Source: `bench/results/2026-11-15-Apple-M4-threads.csv`.

<BarChart
  title="/ping, best of 1 to 8 workers: requests per second (higher is better)"
  rows={[
    {name: 'Xisto', value: 212599, highlight: true, detail: []},
    {name: 'Bun', value: 171046, detail: []},
    {name: 'Rust (axum)', value: 193476, detail: []},
    {name: 'Go (net/http)', value: 178487, detail: []},
    {name: 'Node', value: 175344, detail: []},
  ]}
  caption="Each server at its best number of workers or processes (table below); the load generator shares the M4."
/>

| server | workers | `/ping` req/s | p99 | CPU s per 100k requests | RSS |
|---|---:|---:|---:|---:|---:|
| Xisto | 1 | 212,599 | 2.436 ms | 0.469 | 13.9 MB |
| Xisto | 2 | 205,498 | 2.507 ms | 0.937 | 12.1 MB |
| Xisto | 4 | 201,248 | 3.227 ms | 1.368 | 11.0 MB |
| Xisto | 8 | 198,111 | 2.287 ms | 1.571 | 11.4 MB |
| Xisto (lock) | 4 | 198,781 | 3.554 ms | 1.477 | 11.5 MB |
| Node | 1 | 114,704 | 4.365 ms | 0.889 | 127.8 MB |
| Node (cluster) | 4 | 175,344 | 3.496 ms | 2.029 | 507.3 MB |
| Bun | 1 | 166,293 | 3.060 ms | 0.606 | 35.1 MB |
| Rust (axum, tokio) | 1 | 167,805 | 1.741 ms | 0.593 | 16.7 MB |
| Go (net/http) | 1 | 122,780 | 2.988 ms | 0.813 | 16.8 MB |
| Rust (axum, tokio) | 2 | 191,532 | 2.335 ms | 1.012 | 14.9 MB |
| Go (net/http) | 2 | 159,446 | 2.390 ms | 1.236 | 17.7 MB |
| Rust (axum, tokio) | 4 | 191,511 | 2.131 ms | 1.744 | 15.7 MB |
| Go (net/http) | 4 | 175,589 | 2.941 ms | 2.046 | 20.3 MB |
| Rust (axum, tokio) | 8 | 193,476 | 2.106 ms | 2.046 | 17.8 MB |
| Go (net/http) | 8 | 178,487 | 6.220 ms | 2.631 | 24.8 MB |
| Bun (`reusePort`) | 4 | 171,046 | 3.018 ms | 0.592 | 71.5 MB |

On macOS more workers add CPU per request without adding throughput to Xisto: the kernel does not spread connections
between sockets on one port (`SO_REUSEPORT`), so the workers share one, and the load generator competes for the same
cores. Rust and Go spread work across threads inside one process and gain the most from 1 to 2 workers (Rust 168k → 192k,
Go 123k → 159k), little after; one Xisto event loop still answers more than either at any count. On Linux each Xisto
worker has its own socket and two workers serve more than one (see [TechEmpower](./techempower/index.md)). Threads
also pay off for CPU-bound work (see [Benchmarks Game](./benchmarks-game/index.md)).

The Todo API's list with 4 workers, where the handlers share the list:

| server | workers | `/todos` req/s | p99 | CPU s per 100k requests |
|---|---:|---:|---:|---:|
| Xisto | 1 | 142,130 | 3.491 ms | 0.703 |
| Xisto (lock) | 4 | 111,249 | 5.019 ms | 1.809 |
| Xisto (reads without the lock) | 4 | 185,315 | 3.486 ms | 1.794 |
| Rust (RwLock) | 1 | 131,060 | 2.216 ms | 0.762 |
| Go (RWMutex) | 1 | 71,406 | 4.619 ms | 1.402 |
| Rust (RwLock) | 4 | 173,301 | 2.681 ms | 1.979 |
| Go (RWMutex) | 4 | 129,689 | 5.192 ms | 2.875 |

The Rust and Go servers guard the list with a reader-writer lock (`RwLock`, `sync.RWMutex`), so reads run in
parallel: the fair comparison is Xisto's "reads without the lock" row. (The round of 14 Nov had a script error: its
`todo-xisto,1` row was 4 workers with the lock; this one runs each target with its own settings.)

## Resilience

The Todo API under ramp-up, overload, an error storm, idle connections, a soak and recovery; one event loop each.
Source: `bench/results/2026-11-15-Apple-M4-resilience.csv` (also Xisto with 4 workers).

<BarChart
  title="Resilience, 4,096 connections for 10 s: requests per second (higher is better)"
  rows={[
    {name: 'Xisto', value: 241789, highlight: true, detail: [['p99', '17.526 ms'], ['connection errors', '697']]},
    {name: 'Bun', value: 149325, detail: [['p99', '30.785 ms'], ['connection errors', '1825']]},
    {name: 'Rust (axum)', value: 141503, detail: [['p99', '30.954 ms'], ['connection errors', '370']]},
    {name: 'Go (net/http)', value: 100787, detail: [['p99', '50.681 ms'], ['connection errors', '1296']]},
    {name: 'Node', value: 111217, detail: [['p99', '28.986 ms'], ['connection errors', '2962']]},
  ]}
  caption="One event loop each; the phase every server finds hardest."
/>

| phase | Xisto req/s | Bun req/s | Rust (axum) req/s | Go (net/http) req/s | Node req/s | p99 (Xisto / Rust) | RSS (Xisto / Rust / Go / Bun / Node) | connection errors (Xisto / Bun / Rust / Go / Node) |
|---|---:|---:|---:|---:|---:|---|---|---|
| 64 connections | **244,355** | 160,117 | 165,853 | 120,741 | 120,680 | 0.465 / 0.472 ms | 7.8 / 7.0 / 14.2 / 39.9 / 106.0 MB | 0 / 0 / 0 / 0 / 0 |
| 1,024 connections | **252,017** | 150,173 | 155,346 | 108,502 | 118,561 | 4.642 / 7.616 ms | 16.3 / 55.2 / 40.4 / 40.6 / 129.3 MB | 0 / 0 / 0 / 0 / 1 |
| 4,096 connections | **234,806** | 145,010 | 141,730 | 100,868 | 118,794 | 10.430 / 22.333 ms | 17.5 / 67.4 / 74.9 / 41.0 / 166.3 MB | 820 / 521 / 506 / 800 / 1445 |
| overload, 4,096 for 10 s | **241,789** | 149,325 | 141,503 | 100,787 | 111,217 | 17.526 / 30.954 ms | 19.0 / 71.5 / 131.8 / 41.6 / 207.1 MB | 697 / 1825 / 370 / 1296 / 2962 |
| invalid JSON storm (400 expected) | **236,588** | 129,678 | 150,154 | 94,617 | 83,915 | 2.084 / 1.893 ms | 21.1 / 71.6 / 132.0 / 45.8 / 187.5 MB | 0 / 0 / 0 / 0 / 0 |
| 1,000 idle + 64 active | **235,542** | 167,894 | 171,272 | 109,669 | 116,992 | 0.482 / 0.479 ms | 30.6 / 71.6 / 132.1 / 46.1 / 185.5 MB | 0 / 0 / 0 / 0 / 0 |
| 60 s soak | **173,256** | 139,262 | 134,860 | 70,052 | 90,747 | 0.698 / 0.557 ms | 17.4 / 55.7 / 132.1 / 27.9 / 186.5 MB | 0 / 0 / 0 / 0 / 0 |
| recovery, 64 connections | **222,776** | 159,988 | 162,288 | 113,717 | 108,507 | 0.511 / 0.487 ms | 17.4 / 55.8 / 132.1 / 33.5 / 186.5 MB | 0 / 0 / 0 / 0 / 0 |

Every server answered 200 after every phase. Xisto answers the most requests in every phase, 24% to 62% more than
the next, with the least memory from 1,024 connections up; Rust's p99 is the lowest, or tied, in the light phases;
under 4,096 connections Xisto's is half of Rust's (10.4 against 22.3 ms). After the soak Xisto
held 17.4 MB, against 55.7 MB for Rust and 132 MB for Go; a change in testing (branch `rss-trim`) gives the allocator's
free pages back once the connections are gone.

## Slow dependencies

64 connections, a fake database answering after a fixed delay; one event loop. `one` makes one call per request,
`three` makes three in sequence. Source: `bench/results/2026-11-15-Apple-M4-deps.csv`.

<BarChart
  title="Waiting on a 100 ms dependency: CPU seconds per 100k requests (lower is better)"
  unit="s of CPU per 100k requests"
  rows={[
    {name: 'Xisto', value: 4.069, highlight: true, detail: [['req/s', '620.5']]},
    {name: 'Bun', value: 10.091, detail: [['req/s', '620.4']]},
    {name: 'Rust', value: 6.836, detail: [['req/s', '620.5']]},
    {name: 'Go', value: 10.254, detail: [['req/s', '620.5']]},
    {name: 'Node', value: 24.261, detail: [['req/s', '607.8']]},
  ]}
  caption="Every server answers the same ~620 req/s (64 connections × 100 ms); the CPU is what waiting costs."
  lowerIsBetter
  format={(v) => v.toFixed(1)}
  formatFull={(v) => `${v.toFixed(2)} s`}
/>


| route | delay | Xisto req/s | Bun req/s | Rust req/s | Go req/s | Node req/s | Xisto CPU per 100k | Bun CPU per 100k | Rust CPU per 100k | Go CPU per 100k | Node CPU per 100k |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| one | 1 ms | 51,462 | 48,141 | 54,054 | 49,375 | 26,524 | 1.033 s | 1.850 s | 1.302 s | 1.859 s | 5.206 s |
| one | 5 ms | 10,431 | 9,661 | 10,362 | 10,682 | 8,857 | 3.318 s | 6.740 s | 3.774 s | 4.269 s | 7.352 s |
| one | 100 ms | 620 | 620 | 620 | 620 | 608 | 4.069 s | 10.091 s | 6.836 s | 10.254 s | 24.261 s |
| one | 5,000 ms | 12.8 | 12.8 | 12.8 | 12.8 | 12.8 | &lt;0.01 s¹ | 15.625 s | 10.417 s | 10.417 s | 67.708 s |
| three | 100 ms | 211 | 211 | 211 | 211 | 211 | 8.789 s | 23.438 s | 15.137 s | 21.973 s | 64.453 s |

¹ 64 requests in the whole run at 5 s each: below the timer's resolution.

At 100 ms per call, 64 connections give at most 640 req/s: every server is at the ceiling, and waiting costs Xisto
60% of Rust's CPU, 40% of Bun's and Go's, a sixth of Node's. At 1 ms Rust serves 5% more requests than Xisto, which
uses a fifth less CPU; at 5 ms Go serves 2% more.

## See also

- [Benchmarks](./index.md)
- [Measuring a program](../performance/measuring.md)
- [Methodology](../performance/methodology.md)
