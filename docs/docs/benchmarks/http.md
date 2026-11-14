---
title: HTTP
sidebar_position: 3
---

# HTTP

Unless a section says otherwise, the HTTP results are from an Apple M4 (10 cores, 4 of them performance cores, macOS 15.5) with the load generator
[`oha`](https://github.com/hatoo/oha) 1.16.0 on the same machine, Node v26.12.0 and Bun 1.4.4. Each server is one
process with one event loop unless a table says otherwise. The first Linux results are in
[Node's http module on Linux](#nodes-http-module-on-linux-14-nov-2026).

## Hello server (7 Nov 2026)

64 keep-alive connections, 5 s per route. RSS is sampled right after the load; startup is the time from spawn to
the first 200 on `/ping`. Source: `bench/results/2026-11-07-Apple-M4-http.csv`.

| server | req/s | p50 | p99 | RSS after load | startup | executable |
|---|---:|---:|---:|---:|---:|---:|
| xisto | 231,097 | 0.232 ms | 0.507 ms | 4.20 MB | 2 ms | 111,088 B |
| Bun | 198,223 | 0.271 ms | 0.587 ms | 34.47 MB | 2 ms | 88,660,864 B |
| Node | 136,913 | 0.398 ms | 0.865 ms | 100.72 MB | 11 ms | 61,220,224 B |

The same client program (`bench/http/client.ts`, 20,000 sequential requests against the xisto server) peaks at
1.91 MB on xisto, 35.20 MB on Bun and 220.30 MB on Node.

## Todo API (5 Nov 2026)

A JSON API seeded with 50 todos; 64 connections, 10 s per route. `list` serializes all 50 items, `read` returns
one, `update` parses a JSON body. Source: `bench/results/2026-11-05-Apple-M4-todo.csv`.

| route | xisto | Bun | Node |
|---|---|---|---|
| list | 111,914 req/s, p99 1.202 ms, 3.80 MB | 115,425 req/s, p99 1.121 ms, 34.38 MB | 68,697 req/s, p99 1.904 ms, 78.78 MB |
| read | 142,231 req/s, p99 0.866 ms, 3.72 MB | 147,135 req/s, p99 0.852 ms, 34.34 MB | 78,439 req/s, p99 1.728 ms, 83.08 MB |
| update | 130,609 req/s, p99 0.959 ms, 5.05 MB | 131,247 req/s, p99 0.961 ms, 39.89 MB | 79,869 req/s, p99 1.692 ms, 108.39 MB |

Bun is slightly ahead on throughput on all three routes (0.5% to 3.5%), with 8 to 9 times xisto's memory.
Startup: xisto 1 ms, Bun 2 ms, Node 10 ms.

## Conduit: a NestJS app (8 Nov 2026)

[Conduit](https://github.com/lujakob/nestjs-realworld-example-app) is the public RealWorld example built with
NestJS: controllers, guards, JWT, argon2, a Postgres database and a Redis cache. The same source was built with
xisto and run on Node v26.12.0 (after `npm run build`). 20 connections, 30 s per route, startup the median of 5
fresh starts, cold latency the median of 10. Source: `bench/results/conduit-mac-2026-11-08.txt`.

| | xisto | Node |
|---|---:|---:|
| startup (to the first 200) | 21 ms | 268 ms |
| RSS at rest | 2.6 MB | 136.0 MB |
| RSS after the load | 4.8 MB | 147.5 MB |

Under load, with the Redis cache warm:

| route | xisto req/s | Node req/s | xisto p99 | Node p99 | xisto CPU per 1k req | Node CPU per 1k req | xisto RSS | Node RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| article list (2 queries) | 15,937 | 15,942 | 3.10 ms | 3.55 ms | 39.1 ms | 48.1 ms | 5.9 MB | 162.7 MB |
| article by slug | 30,011 | 28,470 | 1.96 ms | 1.71 ms | 12.7 ms | 22.7 ms | 4.3 MB | 137.1 MB |
| current user (guard, JWT, query) | 12,702 | 7,342 | 2.42 ms | 4.55 ms | 41.1 ms | 135.1 ms | 5.4 MB | 157.3 MB |
| tags | 26,054 | 19,524 | 1.32 ms | 1.71 ms | 19.3 ms | 41.6 ms | 4.2 MB | 132.2 MB |
| login (argon2) | 61 | 50 | 555.61 ms | 456.10 ms | 132,489 ms | 180,128 ms | 197.2 MB | 387.5 MB |

Without the cache (Redis stopped; every request reads Postgres):

| route | xisto req/s | Node req/s | xisto CPU per 1k req | Node CPU per 1k req | xisto RSS | Node RSS |
|---|---:|---:|---:|---:|---:|---:|
| article list | 7,436 | 7,086 | 104.0 ms | 121.6 ms | 5.7 MB | 153.2 MB |
| article by slug | 11,953 | 9,231 | 37.6 ms | 80.3 ms | 4.8 MB | 147.5 MB |

The article list ties on throughput: the database is the limit, and xisto spends less CPU per request. Node has the
better p99 on the article by slug (1.71 against 1.96 ms) and on login (456 against 556 ms). Login is argon2
hashing: both use about 8 cores (8.05 and 8.93), and xisto's p50 is 325 ms against Node's 401 ms.

Single-request latency, cold (cache flushed) / warm, in ms:

| route | xisto | Node |
|---|---:|---:|
| article list | 2.74 / 0.45 | 2.54 / 0.56 |
| article by slug | 1.44 / 0.45 | 2.06 / 0.55 |
| current user | 1.06 / 0.53 | 1.85 / 0.88 |
| tags | 0.64 / 0.34 | 1.12 / 0.45 |
| login | 26.01 / 22.09 | 30.95 / 29.35 |

## Node's http module on Linux (14 Nov 2026)

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

xisto rows only: the script also runs Node (cluster) and Bun (`reusePort`), but the versioned result holds xisto
alone. 256 connections, 10 s per route, the load generator on the same machine. `XISTO_WORKERS=N` runs N event
loops on one port. Source: `bench/results/2026-11-05-Apple-M4-threads.csv` (5 Nov 2026).

| workers | `/ping` req/s | p99 | CPU s per 100k requests | RSS |
|---:|---:|---:|---:|---:|
| 1 | 229,275 | 2.324 ms | 0.432 | 6.0 MB |
| 2 | 217,932 | 2.585 ms | 0.894 | 6.1 MB |
| 4 | 213,282 | 3.970 ms | 1.420 | 6.2 MB |
| 8 | 212,785 | 4.039 ms | 1.612 | 6.4 MB |

One event loop serves the most requests with the least CPU. More workers add CPU per request without adding
throughput on this machine, where the load generator competes for the same cores. Threads pay off for CPU-bound
work (see [Benchmarks Game](./benchmarks-game/index.md)), not for I/O.

## Resilience

xisto only (one worker, Todo API). Source: `bench/results/2026-11-05-Apple-M4-resilience.csv` (5 Nov 2026).

| phase | req/s | p99 | connection errors | RSS | alive after |
|---|---:|---:|---:|---:|---|
| 64 connections | 227,302 | 0.508 ms | 0 | 4.7 MB | 200 |
| 1,024 connections | 218,320 | 5.326 ms | 0 | 27.3 MB | 200 |
| 4,096 connections | 200,888 | 14.355 ms | 843 | 48.6 MB | 200 |
| overload, 4,096 for 10 s | 205,048 | 24.817 ms | 1,069 | 43.6 MB | 200 |
| invalid JSON storm (400 expected) | 198,801 | 2.531 ms | 0 | 41.5 MB | 200 |
| 1,000 idle + 64 active | 203,103 | 0.598 ms | 0 | 45.1 MB | 200 |
| 60 s soak | 151,703 | 0.840 ms | 0 | 4.1 MB (34.8 before) | 200 |
| recovery, 64 connections | 159,897 | 0.781 ms | 0 | 5.5 MB | 200 |

The server answered 200 after every phase, and memory returned to 4.1 MB after the soak.

## Slow dependencies

xisto only (one worker), 64 connections, a fake database answering after a fixed delay. `one` makes one call per
request, `three` makes three in sequence. Source: `bench/results/2026-11-05-Apple-M4-deps.csv` (5 Nov 2026).

| route | delay | req/s | p99 | RSS |
|---|---:|---:|---:|---:|
| one | 1 ms | 55,557 | 1.5 ms | 13.8 MB |
| one | 100 ms | 633 | 103.9 ms | 21.1 MB |
| one | 5,000 ms | 12.8 | 5,007.0 ms | 13.5 MB |
| three | 100 ms | 211 | 313.5 ms | 21.0 MB |

At 100 ms per call, 64 connections give at most 640 req/s: the server reaches 633 with a p99 of 103.9 ms, so the
waiting requests cost no throughput.

## See also

- [Benchmarks](./index.md)
- [Measuring a program](../performance/measuring.md)
- [Methodology](../performance/methodology.md)
