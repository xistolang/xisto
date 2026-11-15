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

## Conduit

Conduit (the RealWorld app with Postgres and Redis) is not measured on Bun yet: it is on [vs Node](./vs-node.md#conduit-a-real-nestjs-app)
for now.

## See also

- [NestJS](./index.md) and [vs Node](./vs-node.md)
- [TechEmpower vs Bun](../techempower/vs-bun.md): `Bun.serve`, Bun's own server
