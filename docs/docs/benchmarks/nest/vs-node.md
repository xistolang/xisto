---
title: Xisto vs Node
sidebar_position: 1
---

import {BarChart} from '@site/src/components/Charts';
import {BestBars, LevelLines, CostBars, RatioBars, VsTable, VsHeadline, Pending} from '@site/src/components/Charts/TechEmpower';

# NestJS: Xisto vs Node

The same NestJS app, not one line changed, built with Xisto or run on Node. On Node it runs as the TFB's own Nest app
does: compiled with `tsc`, one process per CPU with `node:cluster`. On Xisto, in two processes on one port (each with
its own singletons, as the cluster) and in one process with workers and the handler lock. The setup and both apps are
described on [NestJS](./index.md).

<VsHeadline against="nest-node" xisto="xisto-nest-procs" lead="The app built with Xisto in two processes against NestJS on Node, at a glance:" />

## Conduit: a real NestJS app

[Conduit](https://github.com/lujakob/nestjs-realworld-example-app) is the public RealWorld example built with
NestJS: controllers, guards, JWT, argon2, a Postgres database and a Redis cache. The same source, not one line
changed, built with Xisto (main `c684667`) and run on Node v26.12.0 (after `npm run build`), on an Apple M4 (10 cores), 15 Nov 2026;
Postgres 16 and Redis 7 in local containers. 20 connections, 30 s per route, startup the median of 5 fresh starts,
cold latency the median of 10; every request of every run answered correctly. Source:
`bench/results/conduit-mac-2026-11-15.txt` (Xisto, Node and Bun in one session).

<BarChart
  title="Conduit: Xisto's requests per second ÷ Node's, per route (above 1× Xisto answers more)"
  unit="times"
  keepOrder
  refLine={1}
  format={(v) => `${v.toFixed(2)}×`}
  formatFull={(v) => `${v.toFixed(2)}×`}
  rows={[
    {name: 'current user (guard, JWT, query)', value: 8134 / 6528, highlight: true, detail: [['Xisto req/s', '8,134'], ['Node req/s', '6,528']]},
    {name: 'login (argon2)', value: 65 / 45, highlight: true, detail: [['Xisto req/s', '65'], ['Node req/s', '45']]},
    {name: 'tags', value: 17373 / 15731, highlight: true, detail: [['Xisto req/s', '17,373'], ['Node req/s', '15,731']]},
    {name: 'article list (2 queries)', value: 15361 / 14497, highlight: true, detail: [['Xisto req/s', '15,361'], ['Node req/s', '14,497']]},
    {name: 'article by slug', value: 23803 / 34153, detail: [['Xisto req/s', '23,803'], ['Node req/s', '34,153']]},
    {name: 'article list, no cache', value: 5956 / 5431, highlight: true, detail: [['Xisto req/s', '5,956'], ['Node req/s', '5,431']]},
    {name: 'article by slug, no cache', value: 8550 / 7920, highlight: true, detail: [['Xisto req/s', '8,550'], ['Node req/s', '7,920']]},
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
    {name: 'current user · Xisto', value: 63.1, highlight: true, detail: []},
    {name: 'current user · Node', value: 129.1, detail: []},
    {name: 'tags · Xisto', value: 26.7, highlight: true, detail: []},
    {name: 'tags · Node', value: 34.9, detail: []},
    {name: 'article list · Xisto', value: 43.9, highlight: true, detail: []},
    {name: 'article list · Node', value: 51.3, detail: []},
    {name: 'article by slug · Xisto', value: 25.0, highlight: true, detail: []},
    {name: 'article by slug · Node', value: 19.8, detail: []},
  ]}
  caption="The server's user + system time over the run divided by the requests it answered; login (argon2: 127 s against 210 s per 1,000) is in the table."
/>

<BarChart
  title="Conduit: memory at rest and after the load, in MB (lower is better)"
  unit="MB"
  lowerIsBetter
  keepOrder
  format={(v) => v.toFixed(1)}
  formatFull={(v) => `${v.toFixed(1)} MB`}
  rows={[
    {name: 'at rest · Xisto', value: 7.5, highlight: true, detail: []},
    {name: 'at rest · Node', value: 142.5, detail: []},
    {name: 'after the load · Xisto', value: 11.6, highlight: true, detail: []},
    {name: 'after the load · Node', value: 154.4, detail: []},
  ]}
  caption="Resident memory of the server process; startup to the first 200: 26 ms with Xisto, 264 ms with Node."
/>

**Node is ahead on the article by slug with the cache warm** (34,153 against 23,803 requests per second: one Redis
read and a JSON answer), with less CPU per request there (19.8 against 25.0 ms per 1,000). Xisto answers more on
every other route, with and without the cache, uses less CPU on all of them (half of Node's on the current user and
on the article by slug without the cache), starts 10× faster and holds 11.6 MB after the load against 154.4 MB.
Against the round of 14 Nov, Xisto's startup went from 20 to 26 ms and its current-user route from 46 to 63 ms of
CPU per 1,000 requests (Xisto 0.4.1 then, main now); Node's routes moved between −9% and +14%; the Postgres and
Redis containers were restarted in between.

| | Xisto | Node |
|---|---:|---:|
| startup (to the first 200) | **26 ms** | 264 ms |
| RSS at rest | **7.5 MB** | 142.5 MB |
| RSS after the load | **11.6 MB** | 154.4 MB |

Under load, with the Redis cache warm:

| route | Xisto req/s | Node req/s | Xisto p99 | Node p99 | Xisto CPU per 1k req | Node CPU per 1k req | Xisto RSS | Node RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| article list (2 queries) | **15,361** | 14,497 | 2.81 ms | 4.35 ms | 43.9 ms | 51.3 ms | 12.5 MB | 167.4 MB |
| article by slug | 23,803 | **34,153** | 2.03 ms | 1.53 ms | 25.0 ms | 19.8 ms | 11.5 MB | 148.6 MB |
| current user (guard, JWT, query) | **8,134** | 6,528 | 4.27 ms | 4.86 ms | 63.1 ms | 129.1 ms | 11.2 MB | 162.0 MB |
| tags | **17,373** | 15,731 | 2.40 ms | 2.34 ms | 26.7 ms | 34.9 ms | 11.0 MB | 150.2 MB |
| login (argon2) | **65** | 45 | 379.83 ms | 528.74 ms | 127,182.7 ms | 210,270.7 ms | 203.3 MB | 406.1 MB |

Without the cache (Redis stopped; every request reads Postgres):

| route | Xisto req/s | Node req/s | Xisto p99 | Node p99 | Xisto CPU per 1k req | Node CPU per 1k req | Xisto RSS | Node RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| article list | **5,956** | 5,431 | 6.10 ms | 6.42 ms | 92.1 ms | 122.9 ms | 11.5 MB | 304.8 MB |
| article by slug | **8,550** | 7,920 | 4.15 ms | 4.25 ms | 32.2 ms | 61.1 ms | 11.6 MB | 154.4 MB |

Single-request latency, cold (cache flushed) / warm, in ms:

| route | Xisto | Node |
|---|---:|---:|
| article list | 2.09 / 0.65 | 2.68 / 0.61 |
| article by slug | 2.14 / 0.50 | 2.09 / 0.55 |
| current user | 1.71 / 0.72 | 1.81 / 0.96 |
| tags | 1.44 / 0.50 | 1.17 / 0.52 |
| login | 21.83 / 22.75 | 30.87 / 29.82 |

<Pending id="nest-node">

**NestJS on Node is measured in the next round on the VMs.** The build is ready (`bench/techempower/nest-node`, run
as `nest-node` by `bench/techempower/run.py`) and passes the TechEmpower validator; the charts and tables of this section
fill in from the data. Until then they show Xisto alone.

</Pending>

## The TFB app: json

14 Nov 2026 round: the server on the Xeon E5-2686 v4 (2 vCPUs), wrk 4.2.0 on the Graviton2, the TFB's levels and
rules, 15 s per level, the median of 3 runs.

<BestBars test="json" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

<RatioBars test="json" against="nest-node" xisto="xisto-nest-procs" />

<LevelLines test="json" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-node': 6}} />

<CostBars test="json" metric="cpu" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

<CostBars test="json" metric="rss" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

<LevelLines test="json" metric="p99" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-node': 6}} />

### json in numbers

<VsTable test="json" ids={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

## The TFB app: plaintext

<BestBars test="plaintext" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

<RatioBars test="plaintext" against="nest-node" xisto="xisto-nest-procs" />

<LevelLines test="plaintext" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-node': 6}} />

<CostBars test="plaintext" metric="cpu" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

<CostBars test="plaintext" metric="rss" only={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

### plaintext in numbers

<VsTable test="plaintext" ids={['xisto-nest-procs', 'xisto-nest', 'nest-node']} />

## See also

- [NestJS](./index.md) and [vs Bun](./vs-bun.md)
- [TechEmpower vs Node](../techempower/vs-node.md): the plain `node:http` server and fastify
