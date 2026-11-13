---
title: Measuring a program
sidebar_position: 3
---

# Measuring a program

xisto ships its own indicators. None needs an external tool, and each says where to look, not the final number.
The final number comes from a benchmark run as described in [Methodology](./methodology.md).

## `--profile`: time and calls per function

```sh
xisto build --profile -o fib fib.ts
./fib
```

At exit the program prints a table to stderr, sorted by self time (time not spent in the functions it called):

```text
xisto profile (sorted by self time)
function                            calls      self ms     total ms    ns/call
fib                                635621       24.273      449.119        707
peak rss 1.2 MB, 0 allocations
```

Each call reads the clock twice. That makes a small function such as `fib` about 50 times slower. Use the table to
find where the time goes, not how much there is. In a recursive function "total" counts nested calls more than
once; read "self". The instrumentation exists only in a `--profile` build and only in the C backend.

## `XISTO_RC_STATS=1`: allocations and leaks

Any xisto executable run with this variable prints two counts at exit:

```text
xisto: 11 allocations, 0 still alive at exit
```

A third line appears when the cycle collector ran. "Still alive" must be 0: `xisto test` and the compiler's own
suite fail otherwise. A high allocation count on a hot path shows where reference counting works hardest.

## `GET /__metrics`: a server looking at itself

Every `Server` answers `/__metrics` with JSON:

```json
{"uptime_s":12.3,"requests":1600,"rss_mb":1.7,"allocations":9120,"live_objects":54,
 "routes":[{"method":"GET","path":"/todos/:id","count":800,"errors":2,"avg_us":2.7,"p50_us":2,"p99_us":4,"max_us":4}]}
```

Per route: count, errors (status ≥ 400), and the handler's latency (mean, p50, p99, max, in microseconds) over the
last 2,048 requests. Per process: uptime, requests, peak RSS, allocations and live objects. It costs one clock read
per request and a ring of 2,048 integers per route. It is always on.

## `--time`: where the build goes

```text
xisto: parse 0.1 ms, check 0.1 ms, prune 0.0 ms (1 modules, 1 functions, 1 reachable)
xisto: tls not linked
xisto: emit 0.2 ms (447 KB of C)
xisto: cc 272.1 ms
```

## The `XISTO_*_WHY` knobs

Set one of these when building, and the compiler explains each decision on stderr.

| variable | explains |
|---|---|
| `XISTO_REGION_WHY=1` | which loops got a region, and why the others did not |
| `XISTO_STACK_WHY=1` | which classes have objects allocated in the frame |
| `XISTO_COLUMNS_WHY=1` | which arrays of objects became one `Float64Array` per field |
| `XISTO_INPLACE_WHY=1` | whether `readStdinParts` loops read into one reused buffer |
| `XISTO_DISTRIBUTE_WHY=1` | which pair loops over columns were split into two passes |
| `XISTO_CTR_WHY=1` | why an `int[]` of counters did or did not get its range test moved after the loop |
| `XISTO_UNITS_WHY=1` | in `--dev`, what keeps the program in a single C unit |

Example output of a small program:

```text
$ XISTO_REGION_WHY=1 XISTO_STACK_WHY=1 xisto build why.ts
xisto: no region at why.ts:23: no class made in the loop stays inside it
xisto: region 1 at why.ts:17 for Point
$ XISTO_COLUMNS_WHY=1 xisto build col.ts
columns: bodies in main: 3 Body as columns
```

Debugging knobs such as `XISTO_REGION_CHECK=1` (verify every region iteration at run time) exist too. The
`XISTO_NO_<OPTIMIZATION>` variables switch single optimizations off; they are for measuring the compiler and may
change between versions.

## A workflow for a slow server

1. Run the load. `bench/http/todo/run.sh` saves each server's `/__metrics` to `bench/out/metrics-*.json`; for your
   own server, read `/__metrics` after the load.
2. Compare the handler's `p99_us` with the p99 the load generator saw. The difference is network, HTTP parsing
   and queueing. A fast handler with a slow external p99 points at the server, not your code.
3. If the handler is slow, build with `--profile`, repeat the load and read the table. The function with the
   largest self time is the bottleneck.
4. If `live_objects` grows between two readings under the same load, something leaks. Reproduce it in a test
   and run it with `XISTO_RC_STATS=1`.

## See also

- [The performance model](./model.md)
- [Best practices](./best-practices.md)
- [Methodology](./methodology.md)
