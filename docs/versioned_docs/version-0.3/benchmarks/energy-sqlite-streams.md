---
title: Energy, SQLite, streams and micro programs
sidebar_position: 4
---

# Energy, SQLite, streams and micro programs

Every result on this page is from an Apple M4 (macOS 15.5) with Node v26.12.0 and Bun 1.4.4. There are no Linux
results for energy, SQLite or streams yet.

## Energy (5 Nov 2026)

`powermetrics` samples CPU package power every 100 ms while the workload runs. The samples are integrated to
joules, and the idle baseline (0.18 W) is subtracted. Source: `bench/results/2026-11-05-Apple-M4-energy.csv`.

| workload | unit | xisto | Node | Bun |
|---|---|---:|---:|---:|
| sieve | joules per run | 0.508 | 1.419 | 1.176 |
| count lines in a 1 GB log, streaming | joules per run | 2.403 | 6.894 | 2.384 |
| Todo API list route under load | joules per 100k requests | 8.213 | 11.835 | 7.962 |

xisto uses 2.8 times less energy than Node on the sieve and 2.3 times less than Bun. Bun is slightly better on
the HTTP list route (3% less) and about even on the stream.

## SQLite (5 Nov 2026)

200,000 inserts in one transaction, 2,000 point reads and a scan of 198,000 rows into typed objects, on an
in-memory database. xisto uses the `node:sqlite` API; Bun uses `bun:sqlite`. Median of 5. Source:
`bench/results/2026-11-05-Apple-M4-sqlite.csv`.

| runtime | time | peak RSS | what you ship |
|---|---:|---:|---:|
| Bun | 0.07 s | 55.7 MB | 84.55 MB |
| xisto | 0.09 s | 33.8 MB | 1.02 MB |
| Node | 0.21 s | 151.4 MB | 58.38 MB |

Bun is faster here. xisto's executable includes SQLite itself.

## Streams (5 Nov 2026)

Count the lines containing `ERROR` in a 977 MB log of 15 million lines, by streaming and by reading the whole file.
Median of 3. Source: `bench/results/2026-11-05-Apple-M4-streams.csv`.

| runtime | mode | time | peak RSS |
|---|---|---:|---:|
| xisto | stream | 0.32 s | 1.3 MB |
| xisto | `readFileSync` | 0.32 s | 977.8 MB |
| Bun | stream | 0.60 s | 56.5 MB |
| Node | stream | 1.14 s | 111.8 MB |
| Node | `readFileSync` | failed | — |

Node's whole-file read fails: a 1 GB string is longer than V8 allows. A stream keeps xisto's memory at the size of
one line; reading the whole file costs its size.

## Micro programs (7 Nov 2026)

Median of 5, release build unless noted. Source: `bench/results/2026-11-07-Apple-M4.csv`.

| program | xisto | xisto `--dev` | Node | Bun |
|---|---:|---:|---:|---:|
| fib | 0.02 s, 1.3 MB | 0.11 s | 0.12 s, 66.1 MB | 0.03 s, 14.1 MB |
| mandelbrot | 0.52 s, 1.3 MB | 2.16 s | 0.59 s, 68.9 MB | 0.70 s, 16.1 MB |
| nbody | 0.39 s, 1.3 MB | 3.78 s | 0.49 s, 69.2 MB | 0.45 s, 19.2 MB |
| objects | 0.01 s, 1.3 MB | 0.06 s | 0.08 s, 68.6 MB | 0.02 s, 28.4 MB |
| sieve | 0.08 s, 29.9 MB | 0.46 s | 0.33 s, 296.0 MB | 0.23 s, 245.7 MB |

`--dev` compiles at `-O0` for a fast edit loop; it is 4 to 10 times slower than release. The experimental QBE
backend (`--backend=qbe`) ran fib in 0.04 s and nbody in 1.16 s, with a 0.04 s compile.

## The cross-machine suite (7 Nov 2026)

`bench/vm.py` runs one suite of small programs on any machine; this is the M4 run, median of 3. Source:
`bench/results/vm-mac-2026-11-07-final.txt`. The ratio is against the better of Node and Bun.

| program | what it exercises | xisto | Node | Bun | xisto vs best |
|---|---|---:|---:|---:|---:|
| fib | recursion, `int` | 0.022 s | 0.120 s | 0.040 s | 1.83× |
| sieve | arrays of booleans | 0.095 s | 0.358 s | 0.246 s | 2.59× |
| binarytrees | many short-lived trees (depth 18) | 0.243 s | 0.440 s | 0.389 s | 1.60× |
| closures | 10M updates through captured, reassigned variables | 0.011 s | 0.112 s | 0.042 s | 3.95× |
| generators | 3M values through nested generators | 0.004 s | 0.129 s | 0.037 s | 9.53× |
| generics | generic functions over 3 types, 2M elements each | 0.051 s | 0.317 s | 0.119 s | 2.36× |
| date | parse, format, local fields of 1M dates | 0.146 s | 1.043 s | 0.456 s | 3.13× |
| regex | validate 400k emails, parse 300k log lines, replace and split 22 MB | 0.157 s | 0.377 s | 0.202 s | 1.29× |
| promise | 10k tasks on timers, then small concurrent rounds | 0.020 s | 0.096 s | 0.027 s | 1.36× |
| redos | patterns that make a backtracking engine explode | 0.012 s | 7.456 s | 3.641 s | 291.5× |

`redos` tests `/^(a+)+$/` against 22 `a`s followed by `!`, and similar patterns. V8 and JavaScriptCore backtrack,
and the time grows exponentially with the input. xisto's default regex engine is linear in the input, so it has
nothing to explode. A pattern that needs backtracking (backreferences, lookaround) uses a second engine, which
stops with a `RangeError` after 50 million steps.

Older runs of this suite on Linux and Windows (`bench/results/vm-2026-11-07/`) predate the work of 9–10 Nov and
are not summarized here.

## Executable size

| | xisto | Node | Bun |
|---|---:|---:|---:|
| `fib` | 69,288 B | 61,220,224 B | 88,660,864 B |
| hello HTTP server | 111,088 B | 61,220,224 B | 88,660,864 B |
| SQLite program | 1.02 MB | 58.38 MB | 84.55 MB |

For Node and Bun the size is the runtime binary and its private libraries. Sources:
`bench/results/2026-11-07-Apple-M4.csv`, `bench/results/2026-11-07-Apple-M4-http.csv`,
`bench/results/2026-11-05-Apple-M4-sqlite.csv`.

## See also

- [Benchmarks](./index.md)
- [The performance model](../performance/model.md)
- [Methodology](../performance/methodology.md)
