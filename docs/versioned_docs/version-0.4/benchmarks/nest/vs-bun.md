---
title: Xisto vs Bun
sidebar_position: 2
---

import {BarChart} from '@site/src/components/Charts';
import {BestBars, LevelLines, CostBars, RatioBars, VsTable, VsHeadline, Pending} from '@site/src/components/Charts/TechEmpower';

# NestJS: Xisto vs Bun

The same NestJS app, not one line changed, built with Xisto or run on Bun. On Bun it runs as the TFB's own Nest app
does: compiled with `tsc`, one process per CPU with `node:cluster`. On Xisto, in two processes on one port (each with
its own singletons, as the cluster) and in one process with workers and the handler lock. The setup and both apps are
described on [NestJS](./index.md).

<VsHeadline against="nest-bun" xisto="xisto-nest-procs" lead="The app built with Xisto in two processes against NestJS on Bun, at a glance:" />

## Conduit: a real NestJS app

[Conduit](https://github.com/lujakob/nestjs-realworld-example-app) is the public RealWorld example built with
NestJS: controllers, guards, JWT, argon2, a Postgres database and a Redis cache. The same source, not one line
changed, built with Xisto (main `c684667`) and run on Bun 1.4.4 (the same `npm run build` output), on an Apple M4 (10 cores), 15 Nov 2026;
Postgres 16 and Redis 7 in local containers. 20 connections, 30 s per route, startup the median of 5 fresh starts,
cold latency the median of 10; every request of every run answered correctly. Source:
`bench/results/conduit-mac-2026-11-15.txt` (Xisto, Node and Bun in one session).

<BarChart
  title="Conduit: Xisto's requests per second ÷ Bun's, per route (above 1× Xisto answers more)"
  unit="times"
  keepOrder
  refLine={1}
  format={(v) => `${v.toFixed(2)}×`}
  formatFull={(v) => `${v.toFixed(2)}×`}
  rows={[
    {name: 'current user (guard, JWT, query)', value: 8134 / 7213, highlight: true, detail: [['Xisto req/s', '8,134'], ['Bun req/s', '7,213']]},
    {name: 'login (argon2)', value: 65 / 44, highlight: true, detail: [['Xisto req/s', '65'], ['Bun req/s', '44']]},
    {name: 'tags', value: 17373 / 15124, highlight: true, detail: [['Xisto req/s', '17,373'], ['Bun req/s', '15,124']]},
    {name: 'article list (2 queries)', value: 15361 / 19572, detail: [['Xisto req/s', '15,361'], ['Bun req/s', '19,572']]},
    {name: 'article by slug', value: 23803 / 38660, detail: [['Xisto req/s', '23,803'], ['Bun req/s', '38,660']]},
    {name: 'article list, no cache', value: 5956 / 5237, highlight: true, detail: [['Xisto req/s', '5,956'], ['Bun req/s', '5,237']]},
    {name: 'article by slug, no cache', value: 8550 / 7508, highlight: true, detail: [['Xisto req/s', '8,550'], ['Bun req/s', '7,508']]},
  ]}
  caption="20 connections, 30 s per route; the first five with the Redis cache warm, the last two with Redis stopped. The dashed line is 1×; grey: the routes where Bun answers more. Every value is in the tables below."
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
    {name: 'current user · Bun', value: 83.0, detail: []},
    {name: 'tags · Xisto', value: 26.7, highlight: true, detail: []},
    {name: 'tags · Bun', value: 31.1, detail: []},
    {name: 'article list · Xisto', value: 43.9, highlight: true, detail: []},
    {name: 'article list · Bun', value: 35.5, detail: []},
    {name: 'article by slug · Xisto', value: 25.0, highlight: true, detail: []},
    {name: 'article by slug · Bun', value: 14.5, detail: []},
  ]}
  caption="The server's user + system time over the run divided by the requests it answered; login (argon2: 127 s against 221 s per 1,000) is in the table."
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
    {name: 'at rest · Bun', value: 76.5, detail: []},
    {name: 'after the load · Xisto', value: 11.6, highlight: true, detail: []},
    {name: 'after the load · Bun', value: 130.8, detail: []},
  ]}
  caption="Resident memory of the server process; startup to the first 200: 26 ms with Xisto, 214 ms with Bun."
/>

**Bun is ahead on the two routes served from the cache**: the article list (19,572 against 15,361 requests per
second) and the article by slug (38,660 against 23,803), with less CPU per request and a lower p99 on both. Xisto
answers more on the current user (guard, JWT and a query), the tags, the login and both routes without the cache,
with less CPU on all of them and a lower p99 on all of them but the cached ones. Memory is where the two differ most:
Bun holds 131 to 259 MB under load (758 MB during the argon2 logins), Xisto 11 to 13 MB (203 MB during the logins);
at rest 76.5 against 7.5 MB, and Bun starts in 214 ms against 26.

| | Xisto | Bun |
|---|---:|---:|
| startup (to the first 200) | **26 ms** | 214 ms |
| RSS at rest | **7.5 MB** | 76.5 MB |
| RSS after the load | **11.6 MB** | 130.8 MB |

Under load, with the Redis cache warm:

| route | Xisto req/s | Bun req/s | Xisto p99 | Bun p99 | Xisto CPU per 1k req | Bun CPU per 1k req | Xisto RSS | Bun RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| article list (2 queries) | 15,361 | **19,572** | 2.81 ms | 2.35 ms | 43.9 ms | 35.5 ms | 12.5 MB | 258.8 MB |
| article by slug | 23,803 | **38,660** | 2.03 ms | 1.46 ms | 25.0 ms | 14.5 ms | 11.5 MB | 155.8 MB |
| current user (guard, JWT, query) | **8,134** | 7,213 | 4.27 ms | 5.53 ms | 63.1 ms | 83.0 ms | 11.2 MB | 131.3 MB |
| tags | **17,373** | 15,124 | 2.40 ms | 3.06 ms | 26.7 ms | 31.1 ms | 11.0 MB | 131.5 MB |
| login (argon2) | **65** | 44 | 379.83 ms | 877.17 ms | 127,182.7 ms | 220,663.1 ms | 203.3 MB | 757.6 MB |

Without the cache (Redis stopped; every request reads Postgres):

| route | Xisto req/s | Bun req/s | Xisto p99 | Bun p99 | Xisto CPU per 1k req | Bun CPU per 1k req | Xisto RSS | Bun RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| article list | **5,956** | 5,237 | 6.10 ms | 7.59 ms | 92.1 ms | 114.6 ms | 11.5 MB | 150.4 MB |
| article by slug | **8,550** | 7,508 | 4.15 ms | 4.84 ms | 32.2 ms | 56.1 ms | 11.6 MB | 130.8 MB |

Single-request latency, cold (cache flushed) / warm, in ms:

| route | Xisto | Bun |
|---|---:|---:|
| article list | 2.09 / 0.65 | 2.64 / 0.53 |
| article by slug | 2.14 / 0.50 | 2.25 / 0.48 |
| current user | 1.71 / 0.72 | 1.74 / 1.11 |
| tags | 1.44 / 0.50 | 1.09 / 0.50 |
| login | 21.83 / 22.75 | 29.60 / 28.53 |

<Pending id="nest-bun">

**NestJS on Bun is measured in the next round on the VMs.** The build is ready (`bench/techempower/nest-node`, run
as `nest-bun` by `bench/techempower/run.py`) and passes the TechEmpower validator; the charts and tables of this section
fill in from the data. Until then they show Xisto alone.

</Pending>

## The TFB app: json

14 Nov 2026 round: the server on the Xeon E5-2686 v4 (2 vCPUs), wrk 4.2.0 on the Graviton2, the TFB's levels and
rules, 15 s per level, the median of 3 runs.

<BestBars test="json" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

<RatioBars test="json" against="nest-bun" xisto="xisto-nest-procs" />

<LevelLines test="json" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-bun': 4}} />

<CostBars test="json" metric="cpu" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

<CostBars test="json" metric="rss" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

<LevelLines test="json" metric="p99" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-bun': 4}} />

### json in numbers

<VsTable test="json" ids={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

## The TFB app: plaintext

<BestBars test="plaintext" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

<RatioBars test="plaintext" against="nest-bun" xisto="xisto-nest-procs" />

<LevelLines test="plaintext" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} slots={{'xisto-nest-procs': 2, 'xisto-nest': 1, 'nest-bun': 4}} />

<CostBars test="plaintext" metric="cpu" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

<CostBars test="plaintext" metric="rss" only={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

### plaintext in numbers

<VsTable test="plaintext" ids={['xisto-nest-procs', 'xisto-nest', 'nest-bun']} />

## See also

- [NestJS](./index.md) and [vs Node](./vs-node.md)
- [TechEmpower vs Bun](../techempower/vs-bun.md): `Bun.serve`, Bun's own server
