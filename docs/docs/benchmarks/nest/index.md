---
title: NestJS
sidebar_position: 5
---

import {BarChart} from '@site/src/components/Charts';
import {BestBars, LevelLines, CostBars, VsTable, Pending} from '@site/src/components/Charts/TechEmpower';

# NestJS on Xisto, Node and Bun

The [TechEmpower](../techempower/index.md) pages ask which runtime serves HTTP fastest, with one plain server per
runtime. This page asks the question a team with a NestJS service has: **the same Nest app, not one line changed,
built with Xisto or run on Node or on Bun: what does each give?**

Head to head: [vs Node](./vs-node.md) and [vs Bun](./vs-bun.md).

Two apps:

- **The TFB app**: NestJS with the TechEmpower routes `/json` and `/plaintext`
  ([bench/techempower/xisto/nest](https://github.com/xistolang/xisto/tree/main/bench/techempower/xisto/nest)), under
  the TechEmpower rules on the AWS VMs.
- **Conduit**: the public RealWorld app, with guards, JWT, argon2, Postgres and Redis, on an Apple M4.

<Pending id="nest-node">

**Node and Bun on the TFB app come with the next VM round.** Their builds are ready (`bench/techempower/nest-node`:
the same `main.ts` compiled with `tsc`, one process per CPU with `node:cluster`, as the TFB's own Nest app runs) and
pass the TechEmpower validator; the charts below fill in from the data when they are measured. Bun is not measured on
Conduit yet.

</Pending>

## The TFB app: json and plaintext

14 Nov 2026, main `3f24503`, the same round and setup as the [TechEmpower page](../techempower/index.md): the server on
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
  ([vs Node](../techempower/vs-node.md)).
- **Without the lock the workers crash** under load: Nest's singletons are shared by the workers and touched without
  synchronization. The lock is the default for that reason, and the multi-process mode is the one that scales.

## Conduit: a real NestJS app

[Conduit](https://github.com/lujakob/nestjs-realworld-example-app), the public RealWorld example built with NestJS
(guards, JWT, argon2, Postgres, Redis), built with Xisto 0.4.1 and run on Node v26.12.0, on an Apple M4, 14 Nov 2026.
Xisto starts in **20 ms** against Node's 267 ms and rests at **7.3 MB** against 137.3 MB.

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
  caption="20 connections, 30 s per route; the first five with the Redis cache warm, the last two with Redis stopped. The dashed line is 1×; grey: the route where Node answers more. Every value is in the tables on the vs Node page."
/>

The full tables (p99, CPU, memory per route, cold and warm latency) are on [vs Node](./vs-node.md#conduit-a-real-nestjs-app).
Bun on Conduit is not measured yet.

## See also

- [TechEmpower](../techempower/index.md): the runtimes' plain servers
- [HTTP](../http.md): hello, Todo API, threads, resilience and slow dependencies against Node, Bun, Rust and Go
- [NestJS with Xisto](../../getting-started/nestjs.md)
