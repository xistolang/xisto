---
title: Xisto vs Rust
sidebar_position: 1
---

import {BestBars, LevelLines, CostBars, RatioBars, VsTable, VsHeadline} from '@site/src/components/Charts/TechEmpower';

# Xisto vs Rust: TechEmpower json and plaintext

actix-web is the reference to beat: one of the fastest frameworks in the TFB for years, compiled Rust with no garbage collector and no runtime between the handler and the socket. It is the bar Xisto's native server was tuned against (see [what changed since the first round](./index.md#what-changed-since-the-first-round)).

Xisto's native `Server` against it, at a glance:

<VsHeadline against="actix" />

The same round as the [TechEmpower page](./index.md), which has the full setup: the servers on the Xeon E5-2686 v4
(2 vCPUs), wrk 4.2.0 on the Graviton2 over the private network, the TFB's levels and rules, 15 s per level, the
median of 3 runs. The reference here is actix-web (Rust 1.99, built with `RUSTFLAGS=-C target-cpu=native` as the TFB's Dockerfile), the `tfb-web` binary of the TFB's `frameworks/Rust/actix`, unmodified except for the port. On the Xisto side, the native
`Server` with 2 workers. NestJS, on Xisto, Node and Bun, has [its own page](../nest/index.md).

## json

<BestBars test="json" only={['xisto-native', 'actix']} />

<RatioBars test="json" against="actix" />

<LevelLines test="json" only={['xisto-native', 'actix']} />

<CostBars test="json" metric="cpu" only={['xisto-native', 'actix']} />

<CostBars test="json" metric="rss" only={['xisto-native', 'actix']} />

<LevelLines test="json" metric="p99" only={['xisto-native', 'actix']} />

### json in numbers

<VsTable test="json" against="actix" />

## plaintext

<BestBars test="plaintext" only={['xisto-native', 'actix']} />

<RatioBars test="plaintext" against="actix" />

<LevelLines test="plaintext" only={['xisto-native', 'actix']} />

<CostBars test="plaintext" metric="cpu" only={['xisto-native', 'actix']} />

<CostBars test="plaintext" metric="rss" only={['xisto-native', 'actix']} />

<LevelLines test="plaintext" metric="p99" only={['xisto-native', 'actix']} />

### plaintext in numbers

<VsTable test="plaintext" against="actix" />

