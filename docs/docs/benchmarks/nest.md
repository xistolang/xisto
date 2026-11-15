---
title: NestJS
sidebar_position: 5
---

import {BarChart} from '@site/src/components/Charts';
import {BestBars, LevelLines, CostBars, VsTable} from '@site/src/components/Charts/TechEmpower';

# NestJS on Xisto, Node and Bun

The [TechEmpower](./techempower/index.md) pages ask which runtime serves HTTP fastest, with one plain server per
runtime. This page asks the question a team with a NestJS service has: **the same Nest app, not one line changed,
built with Xisto or run on Node or on Bun: what does each give?**

Two apps:

- **The TFB app**: NestJS with the TechEmpower routes `/json` and `/plaintext`
  ([bench/techempower/xisto/nest](https://github.com/xistolang/xisto/tree/main/bench/techempower/xisto/nest)), under
  the TechEmpower rules on the AWS VMs.
- **Conduit**: the public RealWorld app, with guards, JWT, argon2, Postgres and Redis, on an Apple M4.

:::note[Node and Bun on the TFB app come with the next VM round]
The Node and Bun builds of the TFB app are ready (`bench/techempower/nest-node`: the same `main.ts` compiled with
`tsc`, one process per CPU with `node:cluster`, as the TFB's own Nest app runs) and pass the TechEmpower validator;
they are measured in the next round on the VMs, and the charts below fill in from the data. Bun is not measured on
Conduit yet.
:::

## The TFB app: json and plaintext

14 Nov 2026, main `3f24503`, the same round and setup as the [TechEmpower page](./techempower/index.md): the server on
the Xeon E5-2686 v4 (2 vCPUs), wrk 4.2.0 on the Graviton2 over the private network, the TFB's levels and rules, 15 s
per level, the median of 3 runs. The app is built with Xisto and run in three modes:

- **2 processes on one port** (`XISTO_REUSEPORT=1`, one worker each): the kernel spreads the connections, each process
  has its own singletons and nothing is shared, as Node's cluster or Bun's `reusePort`;
- **workers with the lock** (`XISTO_WORKERS=2`, the default): one event loop per CPU in one process, the handlers one
  at a time under a lock, because Nest's singletons are shared;
- **workers without the lock** (`XISTO_LOCK=0`): the handlers in parallel. It crashed under load (below).

On Node and Bun the app runs as the TFB's own NestJS app does: compiled with `tsc`, one process per CPU.

### json

<BestBars test="json" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

<LevelLines test="json" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-bun': 4, 'nest-node': 6}} />

<CostBars test="json" metric="cpu" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

<CostBars test="json" metric="rss" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

<LevelLines test="json" metric="p99" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-bun': 4, 'nest-node': 6}} />

#### json in numbers

<VsTable test="json" ids={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

### plaintext

<BestBars test="plaintext" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

<LevelLines test="plaintext" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-bun': 4, 'nest-node': 6}} />

<CostBars test="plaintext" metric="cpu" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

<CostBars test="plaintext" metric="rss" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

#### plaintext in numbers

<VsTable test="plaintext" ids={['xisto-nest-procs', 'xisto-nest', 'nest-bun', 'nest-node']} />

### What this says

- **In two processes** the app built with Xisto serves 53,032 json requests per second with 16.2 MB of memory, and
  239,187 plaintext requests (HTTP pipelining 16 deep). With the lock, in one process: 44,216 and 116,817, with
  10.7 MB on json.
- **For scale until Nest on Node is measured**: on the same VM Node's bare `http` module, which Nest on Node runs on
  through Express, serves 29,317 json and 51,066 plaintext requests per second with 289 MB
  ([vs Node](./techempower/vs-node.md)).
- **Without the lock the workers crash** under load: Nest's singletons are shared by the workers and touched without
  synchronization. The lock is the default for that reason, and the multi-process mode is the one that scales.

## Conduit: a real NestJS app

[Conduit](https://github.com/lujakob/nestjs-realworld-example-app) is the public RealWorld example built with
NestJS: controllers, guards, JWT, argon2, a Postgres database and a Redis cache. The same source was built with
Xisto 0.4.1 and run on Node v26.12.0 (after `npm run build`). Postgres 16 and Redis 7 in local containers.
20 connections, 30 s per route, startup the median of 5 fresh starts, cold latency the median of 10. Source:
`bench/results/conduit-mac-2026-11-14.txt`.

<BarChart
  title="Conduit: Xisto's requests per second ÷ Node's, per route (above 1× Xisto answers more)"
  unit="times"
  keepOrder
  refLine={1}
  format={(v) => `${v.toFixed(2)}×`}
  formatFull={(v) => `${v.toFixed(2)}×`}
  rows={[
    {name: 'current user (guard, JWT, query)', value: 8357 / 6123, highlight: true, detail: [['Xisto req/s', '8,357'], ['Node req/s', '6,123']]},
    {name: 'login (argon2)', value: 65 / 43, highlight: true, detail: [['Xisto req/s', '65'], ['Node req/s', '43']]},
    {name: 'tags', value: 16508 / 13821, highlight: true, detail: [['Xisto req/s', '16,508'], ['Node req/s', '13,821']]},
    {name: 'article list (2 queries)', value: 16236 / 15922, highlight: true, detail: [['Xisto req/s', '16,236'], ['Node req/s', '15,922']]},
    {name: 'article by slug', value: 25363 / 33754, detail: [['Xisto req/s', '25,363'], ['Node req/s', '33,754']]},
    {name: 'article list, no cache', value: 5545 / 4954, highlight: true, detail: [['Xisto req/s', '5,545'], ['Node req/s', '4,954']]},
    {name: 'article by slug, no cache', value: 8179 / 7263, highlight: true, detail: [['Xisto req/s', '8,179'], ['Node req/s', '7,263']]},
  ]}
  caption="20 connections, 30 s per route; the first five with the Redis cache warm, the last two with Redis stopped. The dashed line is 1×; grey: the route where Node answers more. Every value is in the tables below."
/>

<BarChart
  title="Conduit: CPU time per 1,000 requests, in ms, with the cache warm (lower is better)"
  unit="ms of CPU per 1,000 requests"
  lowerIsBetter
  keepOrder
  format={(v) => v.toFixed(1)}
  formatFull={(v) => `${v.toFixed(1)} ms`}
  rows={[
    {name: 'current user · Xisto', value: 46.2, highlight: true, detail: []},
    {name: 'current user · Node', value: 137.0, detail: []},
    {name: 'tags · Xisto', value: 19.8, highlight: true, detail: []},
    {name: 'tags · Node', value: 39.6, detail: []},
    {name: 'article list · Xisto', value: 41.0, highlight: true, detail: []},
    {name: 'article list · Node', value: 48.0, detail: []},
    {name: 'article by slug · Xisto', value: 20.8, highlight: true, detail: []},
    {name: 'article by slug · Node', value: 19.5, detail: []},
  ]}
  caption="The server's user + system time over the run divided by the requests it answered; login (argon2: 130 s against 225 s per 1,000) is in the table."
/>

<BarChart
  title="Conduit: memory at rest and after the load, in MB (lower is better)"
  unit="MB"
  lowerIsBetter
  keepOrder
  format={(v) => v.toFixed(1)}
  formatFull={(v) => `${v.toFixed(1)} MB`}
  rows={[
    {name: 'at rest · Xisto', value: 7.3, highlight: true, detail: []},
    {name: 'at rest · Node', value: 137.3, detail: []},
    {name: 'after the load · Xisto', value: 12.0, highlight: true, detail: []},
    {name: 'after the load · Node', value: 234.5, detail: []},
  ]}
  caption="Resident memory of the server process; startup to the first 200: 20 ms with Xisto, 267 ms with Node."
/>

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

## See also

- [TechEmpower](./techempower/index.md): the runtimes' plain servers
- [HTTP](./http.md): hello, Todo API, threads, resilience and slow dependencies against Node, Bun, Rust and Go
- [NestJS with Xisto](../getting-started/nestjs.md)
