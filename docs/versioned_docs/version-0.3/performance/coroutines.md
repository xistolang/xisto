---
title: Why coroutines
sidebar_position: 2
---

# Why coroutines

xisto runs `async` code on **coroutines**: each request, task or kept promise runs on a stack of its own (or a
slice of a shared one), and `await` switches stacks. Node and Bun have no such stacks: an `async` function that
waits returns to the event loop, and the rest of it runs later as a continuation of a promise. Both designs sit on
one thread and an event loop over the kernel's readiness API (kqueue, epoll); what differs is how a function that
waits is suspended and resumed. This page tells how xisto got here, with the numbers that decided it.

## The problem: an `await` that blocked

The first xisto server (5 Nov 2026) ran one thread with `kqueue`, like Node and Bun, but `await` was a plain call:
a handler waiting for a database held the thread. The benchmark that exposed it called a fake database answering
after a fixed latency, with 64 connections:

| latency | ceiling | xisto (blocking `await`) | Bun | Node |
|---:|---:|---:|---:|---:|
| 100 ms | 640 req/s | 16 req/s, p99 6.8 s | 621 | 616 |
| 5 s | 12.8 req/s | 3.3 req/s | 12.8 | 12.8 |

More threads helped in exact proportion (16 threads, 16 times more) and never caught up: a thread per waiting
request does not scale. One thread had already won the earlier test without waits (245,686 req/s and 0.40 s of
CPU per 100,000 requests, against 221,850 and 1.40 s with 4 threads), so the answer had to be one thread whose
`await` gives the thread back.

## The options

Three ways to make `await` give the thread back, all keeping the TypeScript syntax unchanged:

| option | how | cost |
|---|---|---|
| **State machines** | the compiler turns each `async` function into a struct holding its locals and a `switch` on the resume point, as Rust and C# do | no stack per wait, but every `await` inside an `if`, a loop or a `try` becomes a state: a large compiler transformation |
| **Coroutines with their own stack** | each request runs on a small stack; `await` is a runtime call that switches stacks, as Go does | memory per waiting request and a context switch written per architecture; nothing changes in the compiler |
| **A thread pool for blocking calls** | waits move to other threads | the thread-per-wait cost just measured |

The decision (Isa, 5 Nov 2026) was the second: coroutines. The `await` became a call into the runtime, so every
form of the language (`await` inside `try`, loops, closures, deep call chains) worked at once, and the compiler
did not change.

## How it works

- **The switch**: `xs_ctx_swap` saves the callee-saved registers (x19–x30 and d8–d15 on ARM64), stores the stack
  pointer and loads the other one: 22 instructions on ARM64, with an x86-64 version beside it.
- **One coroutine per request in flight**, on a 64 KB stack with a guard page, recycled through a free list. Each
  carries its own autorelease pool, `try` stack and recovery point, swapped with the stack.
- **I/O without blocking**: sockets are non-blocking; on `EAGAIN` the coroutine registers the descriptor with
  `kqueue` or `epoll`, naming itself, and yields to the loop, which resumes it when the event arrives.
- **Tasks on a shared stack**: the elements of `Promise.all` and `Promise.race` run as tasks on one shared stack.
  A task that parks copies out only the bytes it used, so 10,000 waiting tasks took 15 MB against Bun's 35 MB.
- **Promises only where they are kept**: `await f()` is a direct call (0.7–0.9 ns); a promise the program keeps
  becomes a coroutine of its own, and `then`, `catch` and `finally` run as microtasks in Node's order.

## The numbers

The same benchmark after the change, one thread, 64 connections, 5 Nov 2026: requests per second, p99 and CPU
per 100,000 requests (`bench/results/*-deps.md`).

| latency | ceiling | xisto | Bun | Node |
|---:|---:|---|---|---|
| 1 ms | 64,000 | **56,238**, 1.4 ms, **0.92 s** | 53,464, 1.8 ms, 1.86 s | 26,748, 4.4 ms, 5.20 s |
| 5 ms | 12,800 | **11,109**, 6.4 ms, **2.27 s** | 10,583, 8.4 ms, 7.93 s | 9,635, 9.5 ms, 8.73 s |
| 20 ms | 3,200 | **2,904**, 23.5 ms, **3.31 s** | 2,815, 26.6 ms, 9.36 s | 2,708, 31.5 ms, 21.3 s |
| 100 ms | 640 | **627**, 106 ms, **3.71 s** | 621, 112 ms, 10.9 s | 616, 114 ms, 32.3 s |
| 1 s | 64 | 64.0, 1.004 s, **4.93 s** | 64.0, 1.005 s, 15.6 s | 64.0, 1.020 s, 28.0 s |
| 5 s | 12.8 | 12.8, 5.006 s, **10.4 s** | 12.8, 5.004 s, 20.8 s | 12.8, 5.014 s, 67.7 s |

xisto reaches the ceiling at every latency, and waiting costs 2 to 7 times less CPU than on Bun and 5 to 9 times
less than on Node: a parked coroutine costs nothing, only the `kevent` and the switch do. Memory during the run:
10–24 MB for xisto, 22–44 MB for Bun, 114–255 MB for Node. With 16 worker threads the throughput stayed at the
ceiling and the CPU per request went up 2 to 7 times, so one thread stayed the default.

## What it costs

- **A switch on every request.** `/ping`, which never waits, went from 245,000 to 229,000 req/s (−7%), because
  every request now runs on a coroutine. Serving on the loop's own stack when a handler does not yield would win
  it back.
- **Memory per waiting request**: up to a 64 KB stack reserved per request in flight, of which the system maps
  only the pages touched (2–3); tasks on the shared stack keep only their used bytes.
- **A kept promise** costs about 73 ns against Node's ~20 ns (M4, 12 Nov 2026): await directly where you can.
- **Not everything is better as a coroutine.** Generators used as values went the other way: from a stack switch
  per `yield` to a state machine, 0.442 → 0.012 s (Bun: 0.031 s). A generator yields far more often than a request
  waits, and its state is small.

## Threads are a separate tool

Coroutines are for waiting; `worker_threads` and `xisto:parallel` are for CPU work. On the Benchmarks Game the
parallel ports use every core (binary-trees went from 2.93 s to 0.73 s with workers), while servers stay on one
thread with coroutines. See [The performance model](./model.md) and the [HTTP benchmarks](../benchmarks/http.md#threads).
