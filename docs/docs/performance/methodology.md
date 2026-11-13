---
title: Methodology
sidebar_position: 4
---

# Methodology

Every number on the [Benchmarks](../benchmarks/index.md) pages comes from a script in `bench/` and a result file in
`bench/results/`. This page says how those numbers were taken and how to take them again.

## Rules

- **One machine per comparison.** Every runtime and language in a table ran on the same machine in the same
  session. Numbers from the Benchmarks Game site (measured on another machine) are never mixed in.
- **Interleaved runs.** The runners alternate runtimes (xisto, Node, Bun, xisto, ...) instead of running each one
  in a block, so a change in machine load hits all of them.
- **Median of at least 3.** Tables report the median of 3 interleaved runs. Close results are confirmed with 9.
  One early "17% gain" turned out to be code alignment: the same loop varied ~30% with a different layout. Since
  then every hypothesis is measured against the current compiler, rebuilt, in interleaved runs.
- **Time, CPU and memory together.** Next to wall time: CPU time (user + system) and `par` (CPU ÷ wall, roughly the
  number of cores used). A program can win on wall time by using two cores; the CPU column shows it.
- **Peak RSS of the process only.** macOS reads it with `wait4` on the child. On Linux a child's `ru_maxrss`
  starts at its parent's RSS when it calls `exec`, so a program launched from the Python runner reported Python's
  memory: `console.log(1)` measured 14 MB that way and 1.8 MB through `bench/rusage.c`, a small launcher that forks
  and reports. VM memory tables taken before that fix (7 Nov 2026) are inflated and not published.
- **Output compared bit for bit.** Each program's output is hashed (SHA-256) and compared with Node's. A result
  whose output differs is not counted.

## The same output, bit for bit

xisto promises the same output as Node, so it never reorders a floating-point sum, never fuses a multiply-add, and
computes `1/Math.sqrt(x)` exactly. Two consequences show in the tables:

- Rust programs that use `rsqrtps` (an approximate reciprocal square root refined by one Newton step) print the
  same nine digits but compute different doubles. `nbody_7` and `nbody_9` are therefore excluded.
- Rust spectral-norm programs that sum four terms at a time are faster than xisto can be on some machines. That
  is a decision, not an oversight.

## Choosing a variant

The Benchmarks Game has several programs per problem and language. `bench/placar.py` picks, on each side, the
fastest variant whose output matched, unless a lighter one is close (rule of 8 Nov 2026, the same for both sides):

- up to 2% slower and at least 10% less memory: the lighter one is chosen;
- 2% to 5% slower and at least 25% less memory: the lighter one is chosen;
- more than 5% slower: never.

A tie is a difference within 5% either way.

## Machines and versions

| machine | details | used for |
|---|---|---|
| Apple M4 | macOS 15.5, 10 cores (4 performance) | micro programs, HTTP, energy, SQLite, streams, Benchmarks Game vs Node/Bun |
| Xeon E5-2686 v4 | AWS t2.medium, 2 vCPUs, AVX2, Ubuntu | Benchmarks Game vs Rust and Node |
| Graviton2 | AWS t4g.small, 2 vCPUs, arm64 | Benchmarks Game vs Rust and Node |

Runtimes: Node v26.12.0 and Bun 1.4.4 on the M4; Node v24.22.2 on the VMs (9 Nov 2026). Rust: stable,
`opt-level = 3`, `codegen-units = 1`, `-C target-cpu=native`, GMP for pidigits, as `bench/benchmarksgame/langs.md`
describes; the Rust version is not recorded in the result files. On arm64 the Rust programs written with x86 SIMD
do not compile.

Both AWS instances are burstable. Under CPU-credit exhaustion the hypervisor steals time, and one round ran the
same executable anywhere from 2.7 to 11 s. The VM result files log the `steal` counter before and after each
round; rounds with heavy steal were repeated.

## The scripts

| script | measures | requirements |
|---|---|---|
| `bench/run.sh [RUNS]` | micro programs on Node, Bun, xisto dev and release: wall time, peak RSS, shipped size, compile time | macOS, `node`, `bun` |
| `python3 bench/vm.py NODE BUN [RUNS]` | the cross-machine suite (`bench/programs`, `rt`, `lang`, `std`) | Python 3.9+, Linux or macOS |
| `python3 bench/benchmarksgame/run.py [--bg-method] NODE BUN [RUNS] [NAME...]` | Benchmarks Game ports against the original Node programs on Node and Bun | Python 3.9+; `--bg-method` scores as the site does (lowest wall, mean CPU) |
| `python3 bench/benchmarksgame/langs.py RUNS rust=DIR/target/release` | the official Rust programs against the xisto ports | the Benchmarks Game source zip (BSD-3) and a Cargo project, per `langs.md` |
| `python3 bench/placar.py RESULTS.txt` | the xisto × Rust scoreboard from a `langs.py` run | none |
| `bench/http/run.sh [DURATION]` | hello HTTP servers and clients (`todo/`, `threads/`, `deps/`, `resilience/` have their own `run.sh`) | macOS, `oha` |
| `sudo sh bench/energy/run.sh` | joules per task with `powermetrics`, idle baseline subtracted | macOS, root |
| `python3 bench/conduit/run.py xisto\|node [DURATION] [CONC]` | the RealWorld NestJS app under load | Postgres and Redis (Docker by default) |

Run every script from the repository root after `make`, with nothing else heavy running. Shell scripts are
macOS-only (`sysctl`, `/usr/bin/time -l`, `otool`); the Python runners work on Linux and macOS. Each run writes a
new dated file into `bench/results/`; compare it with the file it replaces, on the same machine.

## See also

- [Benchmarks](../benchmarks/index.md)
- [Measuring a program](./measuring.md)
- [Divergences from TypeScript](/docs/language/divergences)
