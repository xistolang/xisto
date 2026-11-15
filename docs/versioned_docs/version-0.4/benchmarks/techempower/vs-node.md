---
title: Xisto vs Node
sidebar_position: 4
---

import {BestBars, LevelLines, CostBars, RatioBars, VsTable, VsHeadline} from '@site/src/components/Charts/TechEmpower';

# Xisto vs Node: TechEmpower json and plaintext

Node is what a TypeScript service runs on today. Two references: Node's own `http` module, the floor of any Node framework, and fastify, the fastest of the popular ones.

Xisto's native `Server` against Node http, at a glance:

<VsHeadline against="nodejs" />

The same round as the [TechEmpower page](./index.md), which has the full setup: the servers on the Xeon E5-2686 v4
(2 vCPUs), wrk 4.2.0 on the Graviton2 over the private network, the TFB's levels and rules, 15 s per level, the
median of 3 runs. The reference here is Node v22.22.1 (Ubuntu's package), the TFB's `frameworks/JavaScript/nodejs` (`node:http`) and `frameworks/JavaScript/fastify`, both with `node:cluster` and one worker per CPU, unmodified except for the port. On the Xisto side, the native
`Server` with 2 workers. NestJS, on Xisto, Node and Bun, has [its own page](../nest.md).

## json

<BestBars test="json" only={['xisto-native', 'nodejs', 'fastify']} />

<RatioBars test="json" against="nodejs" />

<LevelLines test="json" only={['xisto-native', 'nodejs']} />

<CostBars test="json" metric="cpu" only={['xisto-native', 'nodejs', 'fastify']} />

<CostBars test="json" metric="rss" only={['xisto-native', 'nodejs', 'fastify']} />

<LevelLines test="json" metric="p99" only={['xisto-native', 'nodejs']} />

### json in numbers

<VsTable test="json" against={['nodejs', 'fastify']} />

## plaintext

<BestBars test="plaintext" only={['xisto-native', 'nodejs', 'fastify']} />

<RatioBars test="plaintext" against="nodejs" />

<LevelLines test="plaintext" only={['xisto-native', 'nodejs']} />

<CostBars test="plaintext" metric="cpu" only={['xisto-native', 'nodejs', 'fastify']} />

<CostBars test="plaintext" metric="rss" only={['xisto-native', 'nodejs', 'fastify']} />

<LevelLines test="plaintext" metric="p99" only={['xisto-native', 'nodejs']} />

### plaintext in numbers

<VsTable test="plaintext" against={['nodejs', 'fastify']} />

