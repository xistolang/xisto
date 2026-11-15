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
