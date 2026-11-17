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

## `xisto:profiler`: sampling CPU profiles (pprof)

`--profile` changes the program it measures. `xisto:profiler` samples the program as it is, in production too:
99 times per second of CPU time, every thread, it records the stack of the running code. `collect()` returns the
samples since the last call as a [pprof](https://github.com/google/pprof) profile (`profile.proto`, not
compressed), which `go tool pprof`, Datadog and other tools read.

```ts
import { start, stop, collect } from "xisto:profiler";
import { writeFileSync } from "xisto:fs";

start(); // 99 Hz; start(hz) for another rate (1 to 1000)
work();
stop();
writeFileSync("cpu.pprof", collect());
```

The sample types are `sample`/`count` and `cpu`/`nanoseconds`. Frames are the TypeScript functions, with file
and line, including the ones the C compiler inlined; runtime code appears under its C name (`xs_loop_run`,
`xs_str_concat`) and system libraries under theirs. A sample is worth the process's CPU time while sampling
divided by the samples taken: macOS checks the timer on its 100 Hz clock and delivers fewer signals than asked,
so the CPU time per function stays right even there.

The signal handler walks the frame pointers xisto keeps for stack traces, reading each frame through the kernel
(it cannot crash on a stray pointer), and counts the stack in a table without allocating or locking. One process
has one profiler (`start()` returns false the second time). Not on Windows, not with `--backend=qbe`. The cost,
measured on an M4 with three alternating runs: none visible in a CPU-bound program (2.44 s either way), 2 to 4%
fewer requests per second in the Todo API under `oha` (a server that spends half its CPU in `read` and `write`:
each signal interrupts a system call). A program that imports the module but does not call `start()` runs the
same code as without it: the compiler only adds two tables of names.
The dd-trace package uses it for Datadog's Profiles tab (`tracer.init({ profiling: true })`).

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
 "routes":[{"method":"GET","path":"/todos/:id","count":800,"errors":2,"timed":800,"avg_us":2.7,"p50_us":2,"p99_us":4,"max_us":4}]}
```

Per route: count, errors (status ≥ 400), and the latency (mean, p50, p99, max, in microseconds) from the moment the
event loop saw the request to the end of the handler. The first 2,048 requests of a route are all timed; after that,
one request in 16 of each worker is, and `timed` says how many went into the latency figures (count and errors stay
exact). Per process: uptime, requests, peak RSS, allocations and live objects. It costs one clock read per turn of
the event loop plus one per timed request, and a ring of 2,048 integers per route. It is always on.

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
change between versions. Two more trade binary size for code layout: `XISTO_PKG_O3=1` compiles a package's code that
runs once at `-O3` instead of for size, and `XISTO_REL_INLINE=1` inlines every string release instead of calling
one function for it.

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
