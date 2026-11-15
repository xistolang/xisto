---
title: Xisto vs Go
sidebar_position: 2
---

import {BestBars, LevelLines, CostBars, RatioBars, VsTable, VsHeadline} from '@site/src/components/Charts/TechEmpower';

# Xisto vs Go: TechEmpower json and plaintext

Go's standard library server is what most Go services run: a goroutine per connection, a garbage collector, and no framework on top.

Xisto's native `Server` against it, at a glance:

<VsHeadline against="go-std" />

The same round as the [TechEmpower page](./index.md), which has the full setup: the servers on the Xeon E5-2686 v4
(2 vCPUs), wrk 4.2.0 on the Graviton2 over the private network, the TFB's levels and rules, 15 s per level, the
median of 3 runs. The reference here is Go 1.27 `net/http` from the standard library, the TFB's `frameworks/Go/go-std` built as its `go.dockerfile` (`./app -bind :9111`), unmodified except for the port. On the Xisto side, the native
`Server` and the NestJS app built with Xisto, in two processes on one port and in workers with the lock.

## json

<BestBars test="json" only={['xisto-native', 'xisto-nest-procs', 'xisto-nest', 'go-std']} />

<RatioBars test="json" against="go-std" />

<LevelLines test="json" only={['xisto-native', 'xisto-nest-procs', 'go-std']} />

<CostBars test="json" metric="cpu" only={['xisto-native', 'xisto-nest-procs', 'xisto-nest', 'go-std']} />

<CostBars test="json" metric="rss" only={['xisto-native', 'xisto-nest-procs', 'xisto-nest', 'go-std']} />

<LevelLines test="json" metric="p99" only={['xisto-native', 'xisto-nest-procs', 'go-std']} />

### json in numbers

<VsTable test="json" against="go-std" />

## plaintext

<BestBars test="plaintext" only={['xisto-native', 'xisto-nest-procs', 'xisto-nest', 'go-std']} />

<RatioBars test="plaintext" against="go-std" />

<LevelLines test="plaintext" only={['xisto-native', 'xisto-nest-procs', 'go-std']} />

<CostBars test="plaintext" metric="cpu" only={['xisto-native', 'xisto-nest-procs', 'xisto-nest', 'go-std']} />

<CostBars test="plaintext" metric="rss" only={['xisto-native', 'xisto-nest-procs', 'xisto-nest', 'go-std']} />

<LevelLines test="plaintext" metric="p99" only={['xisto-native', 'xisto-nest-procs', 'go-std']} />

### plaintext in numbers

<VsTable test="plaintext" against="go-std" />

