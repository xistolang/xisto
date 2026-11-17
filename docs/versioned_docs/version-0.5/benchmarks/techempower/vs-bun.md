---
title: Xisto vs Bun
sidebar_position: 3
---

import {BestBars, LevelLines, CostBars, RatioBars, VsTable, VsHeadline} from '@site/src/components/Charts/TechEmpower';

# Xisto vs Bun: TechEmpower json and plaintext

Bun is the fastest JavaScript runtime in this round: its HTTP server is native code (Zig, uWebSockets), and only the handler runs in JavaScriptCore.

Xisto's native `Server` against it, at a glance:

<VsHeadline against="bun" />

The same round as the [TechEmpower page](./index.md), which has the full setup: the servers on the Xeon E5-2686 v4
(2 vCPUs), wrk 4.2.0 on the Graviton2 over the private network, the TFB's levels and rules, 15 s per level, the
median of 3 runs. The reference here is Bun 1.4.4, the TFB's `frameworks/TypeScript/bun`: `Bun.serve` with `reusePort`, one process per CPU started by its `spawn.ts`, unmodified except for the port. On the Xisto side, the native
`Server` with 2 workers. NestJS, on Xisto, Node and Bun, has [its own page](../nest/index.md).

## json

<BestBars test="json" only={['xisto-native', 'bun']} />

<RatioBars test="json" against="bun" />

<LevelLines test="json" only={['xisto-native', 'bun']} />

<CostBars test="json" metric="cpu" only={['xisto-native', 'bun']} />

<CostBars test="json" metric="rss" only={['xisto-native', 'bun']} />

<LevelLines test="json" metric="p99" only={['xisto-native', 'bun']} />

### json in numbers

<VsTable test="json" against="bun" />

## plaintext

<BestBars test="plaintext" only={['xisto-native', 'bun']} />

<RatioBars test="plaintext" against="bun" />

<LevelLines test="plaintext" only={['xisto-native', 'bun']} />

<CostBars test="plaintext" metric="cpu" only={['xisto-native', 'bun']} />

<CostBars test="plaintext" metric="rss" only={['xisto-native', 'bun']} />

<LevelLines test="plaintext" metric="p99" only={['xisto-native', 'bun']} />

### plaintext in numbers

<VsTable test="plaintext" against="bun" />

