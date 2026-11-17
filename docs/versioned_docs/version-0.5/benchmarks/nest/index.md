---
title: NestJS
sidebar_position: 5
---

import {BarChart} from '@site/src/components/Charts';
import {BestBars, LevelLines, CostBars, VsTable} from '@site/src/components/Charts/TechEmpower';

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

## The TFB app: json and plaintext

15 Nov 2026, main `f766bf8`, the same round and setup as the [TechEmpower page](../techempower/index.md): the server on
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

- **In two processes** the app built with Xisto serves 54,049 json requests per second with 16.2 MB of memory:
  2.0× Bun's 27,306 (233 MB) and 7.7× Node's 7,027 (354 MB), with half of Bun's CPU per request and an eighth of
  Node's (36.1 ms per 1,000 requests against 72.1 and 281.7).
- **On plaintext** (HTTP pipelining 16 deep) the gap grows: 239,831 requests per second against Bun's 29,629 (8.1×)
  and Node's 11,092 (21.6×), with 74 MB at the peak against 1.5 GB and 2.3 GB.
- **At 16,384 connections** Node answered the first of the three runs (7,944 requests per second, 2,053 requests
  past wrk's 8 s timeout) and none in the next two: it was still working through the first run's queue. That level
  has no Node row.
- **With the lock, in one process**: 44,170 json and 115,557 plaintext requests per second, with 10.7 MB on json.
  Still ahead of Bun's cluster on both.
- **Without the lock the workers crash** under load: Nest's singletons are shared by the workers and touched without
  synchronization. The lock is the default for that reason, and the multi-process mode is the one that scales.

## Conduit: a real NestJS app

[Conduit](https://github.com/lujakob/nestjs-realworld-example-app), the public RealWorld example built with NestJS
(guards, JWT, argon2, Postgres, Redis), built with Xisto and run on Node v26.12.0 and Bun 1.4.4, in one session on an
Apple M4, 15 Nov 2026 (`bench/results/conduit-mac-2026-11-15.txt`).

<BarChart
  title="Conduit: startup, from exec to the first 200, in ms (lower is better)"
  unit="ms"
  lowerIsBetter
  format={(v) => v.toFixed(0)}
  formatFull={(v) => `${v.toFixed(0)} ms`}
  rows={[
    {name: 'Xisto', value: 26, highlight: true, detail: []},
    {name: 'Bun', value: 214, detail: []},
    {name: 'Node', value: 264, detail: []},
  ]}
  caption="The median of 5 fresh starts, until GET /api/tags answers."
/>

<BarChart
  title="Conduit: memory after the load, in MB (lower is better)"
  unit="MB"
  lowerIsBetter
  format={(v) => v.toFixed(1)}
  formatFull={(v) => `${v.toFixed(1)} MB`}
  rows={[
    {name: 'Xisto', value: 11.6, highlight: true, detail: [['at rest', '7.5 MB']]},
    {name: 'Bun', value: 130.8, detail: [['at rest', '76.5 MB']]},
    {name: 'Node', value: 154.4, detail: [['at rest', '142.5 MB']]},
  ]}
  caption="Resident memory of the server process after every route's load; at rest in the tooltip."
/>

Requests per second, 20 connections, 30 s per route:

| route | Xisto | Node | Bun |
|---|---:|---:|---:|
| current user (guard, JWT, query) | **8,134** | 6,528 | 7,213 |
| tags | **17,373** | 15,731 | 15,124 |
| login (argon2) | **65** | 45 | 44 |
| article list, cache warm | 15,361 | 14,497 | **19,572** |
| article by slug, cache warm | 23,803 | 34,153 | **38,660** |
| article list, no cache | **5,956** | 5,431 | 5,237 |
| article by slug, no cache | **8,550** | 7,920 | 7,508 |

Xisto answers the most on five of the seven routes; on the two served from the Redis cache Bun is first and Node is
ahead of Xisto on the article by slug. Every route, latency, CPU and memory number is on [vs Node](./vs-node.md) and
[vs Bun](./vs-bun.md).

## See also

- [TechEmpower](../techempower/index.md): the runtimes' plain servers
- [HTTP](../http.md): hello, Todo API, threads, resilience and slow dependencies against Node, Bun, Rust and Go
- [NestJS with Xisto](../../getting-started/nestjs.md)
