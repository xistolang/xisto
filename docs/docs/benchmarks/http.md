---
title: HTTP
sidebar_position: 3
---

import {BarChart} from '@site/src/components/Charts';

# HTTP

Unless a section says otherwise, the HTTP results are from an Apple M4 (10 cores, 4 of them performance cores, macOS 15.5), re-measured on 14 Nov 2026 with Xisto 0.4.1, with the load generator
[`oha`](https://github.com/hatoo/oha) 1.16.0 on the same machine, Node v26.12.0 and Bun 1.4.4. Each server is one
process with one event loop unless a table says otherwise. The first Linux results are in
[Node's http module on Linux](#nodes-http-module-on-linux).

## Hello server

64 keep-alive connections, 10 s per route. RSS is sampled right after the load; startup is the time from spawn to
the first 200 on `/ping`. Source: `bench/results/2026-11-14-Apple-M4-http.csv`.

<BarChart
  title="GET /ping: requests per second"
  rows={[
    {name: 'Xisto', value: 244918, highlight: true, detail: [['p99', '0.506 ms'], ['RSS after load', '6.39 MB']]},
    {name: 'Bun', value: 183577, detail: [['p99', '0.715 ms'], ['RSS after load', '35.56 MB']]},
    {name: 'Node', value: 127738, detail: [['p99', '1.194 ms'], ['RSS after load', '78.14 MB']]},
  ]}
  caption="One process, one event loop each; every value is in the table below."
/>

| server | route | req/s | p50 | p99 | RSS after load | startup | executable |
|---|---|---:|---:|---:|---:|---:|---:|
| xisto | `/ping` | **244,918** | 0.222 ms | 0.506 ms | 6.39 MB | 1 ms | 130,048 B |
| xisto | `/hello?name=isa` | **227,811** | 0.237 ms | 0.648 ms | 6.83 MB | | |
| Bun | `/ping` | 183,577 | 0.292 ms | 0.715 ms | 35.56 MB | 2 ms | 88,660,864 B |
| Bun | `/hello?name=isa` | 169,877 | 0.314 ms | 0.844 ms | 31.39 MB | | |
| Node | `/ping` | 127,738 | 0.414 ms | 1.194 ms | 78.14 MB | 6 ms | 61,220,224 B |
| Node | `/hello?name=isa` | 123,342 | 0.436 ms | 1.181 ms | 81.12 MB | | |

The same client program (`bench/http/client.ts`, 20,000 sequential requests against the xisto server) takes 0.58 s
and peaks at 2.44 MB on xisto, 1.01 s and 35.98 MB on Bun, 1.72 s and 218.11 MB on Node.

On 7 Nov xisto served 231,097 req/s on `/ping` with 4.20 MB after the load; the server now answers more (the
system calls a request no longer makes, see [TechEmpower](./techempower/index.md)) and holds about 2 MB more after the
load, which is being looked into.

## Todo API

A JSON API seeded with 50 todos; 64 connections, 10 s per route. `list` serializes all 50 items, `read` returns
one, `update` parses a JSON body. Source: `bench/results/2026-11-14-Apple-M4-todo.csv`.

<BarChart
  title="Todo API, list (50 items as JSON): requests per second"
  rows={[
    {name: 'Xisto', value: 180209, highlight: true, detail: [['p99', '0.685 ms'], ['RSS', '6.52 MB']]},
    {name: 'Bun', value: 145815, detail: [['p99', '0.879 ms'], ['RSS', '34.31 MB']]},
    {name: 'Node', value: 102813, detail: [['p99', '1.236 ms'], ['RSS', '93.11 MB']]},
  ]}
/>

| route | xisto | Bun | Node |
|---|---|---|---|
| list | **180,209 req/s**, p99 0.685 ms, 6.52 MB | 145,815 req/s, p99 0.879 ms, 34.31 MB | 102,813 req/s, p99 1.236 ms, 93.11 MB |
| read | **230,998 req/s**, p99 0.489 ms, 7.41 MB | 160,310 req/s, p99 0.761 ms, 31.36 MB | 116,358 req/s, p99 1.184 ms, 82.84 MB |
| update | **212,643 req/s**, p99 0.568 ms, 7.27 MB | 132,620 req/s, p99 0.954 ms, 37.12 MB | 98,316 req/s, p99 1.274 ms, 101.55 MB |

Xisto is ahead on all three routes (24% to 60% over Bun), with a fifth of Bun's memory. On 5 Nov Bun was ahead by
0.5% to 3.5%; the change is the server's (14 Nov), not the program's.

## Conduit: a NestJS app

[Conduit](https://github.com/lujakob/nestjs-realworld-example-app) is the public RealWorld example built with
NestJS: controllers, guards, JWT, argon2, a Postgres database and a Redis cache. The same source was built with
Xisto 0.4.1 and run on Node v26.12.0 (after `npm run build`). Postgres 16 and Redis 7 in local containers.
20 connections, 30 s per route, startup the median of 5 fresh starts, cold latency the median of 10. Source:
`bench/results/conduit-mac-2026-11-14.txt`.

| | xisto | Node |
|---|---:|---:|
| startup (to the first 200) | **20 ms** | 267 ms |
| RSS at rest | **7.3 MB** | 137.3 MB |
| RSS after the load | **12.0 MB** | 234.5 MB |

Under load, with the Redis cache warm:

| route | xisto req/s | Node req/s | xisto p99 | Node p99 | xisto CPU per 1k req | Node CPU per 1k req | xisto RSS | Node RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| article list (2 queries) | 16,236 | 15,922 | 2.89 ms | 3.51 ms | 41.0 ms | 48.0 ms | 23.0 MB | 170.0 MB |
| article by slug | 25,363 | **33,754** | 1.83 ms | 1.65 ms | 20.8 ms | 19.5 ms | 11.0 MB | 164.1 MB |
| current user (guard, JWT, query) | **8,357** | 6,123 | 4.20 ms | 5.56 ms | 46.2 ms | 137.0 ms | 10.8 MB | 153.0 MB |
| tags | **16,508** | 13,821 | 2.41 ms | 2.71 ms | 19.8 ms | 39.6 ms | 10.2 MB | 145.6 MB |
| login (argon2) | **65** | 43 | 352.47 ms | 574.88 ms | 130,057 ms | 224,858 ms | 203.1 MB | 402.8 MB |

Without the cache (Redis stopped; every request reads Postgres):

| route | xisto req/s | Node req/s | xisto CPU per 1k req | Node CPU per 1k req | xisto RSS | Node RSS |
|---|---:|---:|---:|---:|---:|---:|
| article list | 5,545 | 4,954 | 96.9 ms | 141.6 ms | 11.5 MB | 402.9 MB |
| article by slug | 8,179 | 7,263 | 42.5 ms | 66.1 ms | 12.0 MB | 234.5 MB |

**Node is ahead on the article by slug with the cache warm** (33,754 against 25,363 req/s: a Redis read and a JSON
answer); xisto leads the other routes, uses less CPU per request on all but that one, and a tenth of the memory or
less. Compared with the round of 8 Nov the database routes are slower on both sides (Node's article list without
the cache 7,086 then, 4,954 now): the Postgres and Redis containers were made again for this round. A/B rounds in
the same environment (`bench/results/conduit-mac-2026-11-14-ab.txt`) found no change from the compiler (main
`40cdc16` against 0.4.1) nor from the packages (their main against the APM branch).

Single-request latency, cold (cache flushed) / warm, in ms:

| route | xisto | Node |
|---|---:|---:|
| article list | 2.09 / 0.51 | 2.43 / 0.49 |
| article by slug | 1.59 / 0.39 | 2.02 / 0.53 |
| current user | 1.09 / 0.63 | 1.92 / 1.00 |
| tags | 0.90 / 0.47 | 1.22 / 0.55 |
| login | 25.30 / 23.78 | 29.53 / 28.64 |

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
one port; Node runs a cluster, Bun `reusePort`. Source: `bench/results/2026-11-14-Apple-M4-threads.csv`.

| server | workers | `/ping` req/s | p99 | CPU s per 100k requests | RSS |
|---|---:|---:|---:|---:|---:|
| xisto | 1 | **193,899** | 2.677 ms | 0.509 | 12.6 MB |
| xisto | 2 | 169,816 | 5.659 ms | 1.034 | 10.2 MB |
| xisto | 4 | 179,947 | 3.377 ms | 1.538 | 11.9 MB |
| xisto | 8 | 177,304 | 3.317 ms | 1.625 | 9.9 MB |
| Bun | 1 | 167,227 | 3.009 ms | 0.603 | 35.0 MB |
| Bun (`reusePort`) | 4 | 163,557 | 3.095 ms | 0.618 | 71.2 MB |
| Node | 1 | 113,204 | 4.468 ms | 0.898 | 125.4 MB |
| Node (cluster) | 4 | 168,903 | 4.830 ms | 2.104 | 499.3 MB |

On macOS more workers add CPU per request without adding throughput: the kernel does not spread connections
between sockets on one port (`SO_REUSEPORT`), so the workers share one, and the load generator competes for the same
cores. On Linux each worker has its own socket since 14 Nov and two workers serve more than one (see
[TechEmpower](./techempower/index.md)). Threads also pay off for CPU-bound work (see [Benchmarks Game](./benchmarks-game/index.md)).

## Resilience

The Todo API under ramp-up, overload, an error storm, idle connections, a soak and recovery. Source:
`bench/results/2026-11-14-Apple-M4-resilience.csv` (also Node, Bun and xisto with 4 workers).

| phase | xisto req/s | xisto p99 | xisto RSS | Bun req/s | Node req/s | connection errors (xisto / Bun / Node) |
|---|---:|---:|---:|---:|---:|---|
| 64 connections | **190,577** | 0.622 ms | 7.8 MB | 141,133 | 97,181 | 0 / 0 / 0 |
| 1,024 connections | **182,010** | 10.946 ms | 18.0 MB | 136,171 | 99,502 | 0 / 0 / 0 |
| 4,096 connections | **172,689** | 13.745 ms | 19.0 MB | 114,831 | 99,223 | 818 / 724 / 1,386 |
| overload, 4,096 for 10 s | **181,627** | 23.818 ms | 19.6 MB | 134,793 | 93,138 | 1,069 / 1,276 / 2,961 |
| invalid JSON storm (400 expected) | **176,873** | 2.945 ms | 20.5 MB | 109,863 | 72,714 | 0 / 0 / 0 |
| 1,000 idle + 64 active | **187,552** | 0.661 ms | 31.3 MB | 150,090 | 101,935 | 0 / 0 / 0 |
| 60 s soak | **141,645** | 0.882 ms | 28.5 MB (31.5 before) | 118,019 | 83,464 | 0 / 0 / 0 |
| recovery, 64 connections | **187,685** | 0.631 ms | 28.7 MB | 143,681 | 96,024 | 0 / 0 / 0 |

Every server answered 200 after every phase. After the soak xisto held 28.5 MB; on 5 Nov it went back to 4.1 MB.
It does not grow from phase to phase, but the server keeps more memory than it did: under investigation.

## Slow dependencies

64 connections, a fake database answering after a fixed delay; one event loop. `one` makes one call per request,
`three` makes three in sequence. Source: `bench/results/2026-11-14-Apple-M4-deps.csv`.

| route | delay | xisto req/s | Bun req/s | Node req/s | xisto CPU per 100k | Bun CPU per 100k | Node CPU per 100k |
|---|---:|---:|---:|---:|---:|---:|---:|
| one | 1 ms | 52,005 | **54,546** | 26,152 | **1.00 s** | 1.85 s | 5.35 s |
| one | 5 ms | 10,335 | **10,702** | 9,683 | **3.42 s** | 6.24 s | 8.12 s |
| one | 100 ms | **627** | 621 | 612 | **4.03 s** | 10.74 s | 30.36 s |
| one | 5,000 ms | 12.8 | 12.8 | 12.8 | **5.21 s** | 20.83 s | 31.25 s |
| three | 100 ms | 211 | 211 | 211 | **7.32 s** | 22.46 s | 68.36 s |

At 100 ms per call, 64 connections give at most 640 req/s: every server is at the ceiling, and waiting costs xisto
a quarter to a third of Bun's CPU and a sixth to a ninth of Node's. At 1 and 5 ms Bun serves 4% to 5% more
requests.

## See also

- [Benchmarks](./index.md)
- [Measuring a program](../performance/measuring.md)
- [Methodology](../performance/methodology.md)
