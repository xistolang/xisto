---
title: The performance model
sidebar_position: 1
---

# The performance model

xisto compiles a strict subset of TypeScript to C, and clang turns that C into one native executable. There is no
interpreter, no JIT and no tracing garbage collector: the type you write decides the machine representation.
"Before → after" numbers come from the comments next to each optimization in `src/`, with their machine.

## Values and objects

| construct | representation |
|---|---|
| `int` | a 64-bit integer in a register, limited to ±(2^53−1) so results match Node; leaving the range is a run-time error |
| `number` | a C `double`, strict IEEE: no fused multiply-add (`-ffp-contract=off`), sums never reordered |
| `boolean` | a byte |
| `T \| null` of a primitive | the same register with a sentinel (`INT64_MIN` for int, a NaN payload for number) |
| class instance | a C struct behind an 8-byte header (count and type); up to 248 bytes from per-thread free lists |
| `string` | immutable UTF-8; `length` and indices count code points, computed once and cached |
| array | length, capacity and data; up to 4 elements stored inline; bounds checked |
| `Map` / `Set` | a Swiss table: one control byte per slot, 16 compared at once with NEON or SSE2; insertion order kept |
| generic function or class | one copy per type list (monomorphised): no run-time cost |
| `any` | the same struct, read by name through a per-class field table: about 10× a typed access (see [`any`](/docs/language/any)) |

Mixing `int` and `number` gives `number` (warning XS2103). Indices and lengths must be `int`. The `int`/`number`
split is what lets loops stay in integer registers: mandelbrot went from 0.68 to 0.50 s when it arrived (M4, 4 Nov
2026, design notes). Methods are direct calls; only a method that a subclass overrides dispatches on the class.

## Memory: reference counting decided at compile time

The compiler inserts every retain and release. A fresh value (a call result, `new`, a literal, a concatenation)
already holds its reference; a borrowed one is retained only where it is kept. Temporaries go to an autorelease
pool drained after each statement. Locals are released at scope end, `break`, `continue` and `return`. A `throw`
unwinds through a cleanup stack that releases the locals of each frame. Several optimizations remove most of
that work:

- **Stack allocation.** `const v = new C(...)` lives in the C frame when no use escapes (no return, no store, no
  capture, no print). The class must be plain: no base, no subclasses, a constructor that only assigns fields.
  Functions with `try`, `async` functions and generators are skipped.
- **Regions.** A loop gets an arena for a class when no object made in one iteration can outlive it. The arena is
  rewound at the start of the next iteration; frees are no-ops. The proof fails when the loop or anything it calls
  uses `await`, `yield`, closures, function values, `unknown`, unions, JSON values or casts, or returns from the
  body. A *dual* region also covers a class made both inside and outside the loop: the constructor checks at run
  time whether an iteration is running. Dual regions need a program without `try` anywhere, because an exception
  leaving the loop would leave the arena on. Binary-trees: 3.88 → 2.93 s (M4, 7 Nov 2026, design notes).
- **Objects without a header.** A region class that is never printed, serialized or counted drops its header:
  binary-trees' node is 16 bytes, not 24 (Xeon: −14% CPU, 350 → 300 MB; `src/emit.c`).
- **Borrowed fields.** `const x = base.field` borrows instead of retaining when `base` outlives the scope
  (binarytrees-2: 0.65 → 0.42 s on the M4, 4.90 → 3.17 s on the Graviton2; `src/emit.c`).
- **In-place update.** `x = x.with(v)`, where `with` only returns `new C(...)` of the same class, rewrites the old
  object's fields when `x` holds the only reference. An immutable-update style costs what a mutation costs.

The cycle collector runs only for types that can reach themselves: classes with a path back to their own type,
and containers or closures of those. It uses trial deletion over suspects, as CPython does, once they pass a
threshold (10,000 at start, growing with live objects). A program without cyclic types has no collector code.

## Numeric code

- **Columns.** A local array literal of up to 64 `new C(...)` of an all-`number` class becomes one
  `Float64Array` per field, with the same doubles in the same order (nbody.ts, M4: 1.62 → 1.23 s; `src/columns.c`).
- **Frame arrays.** A `Float64Array` of up to 64 elements that is only indexed lives in the frame, and clang keeps
  it in registers: n-body's advance went 2.40 → 1.23 s, equal to Rust (M4; `src/emit.c`).
- **Integer ranges.** When `a + b`, `a - b` or `a * b` provably fits in 64 bits, the overflow check is dropped. The
  check is a branch that also blocks vectorization.
- **Bounds-check elimination.** An index proved inside the array skips the check.
- **Compiler flags.** `-O3` for release (objects: 5.2 → 3.7 ns per iteration, M4, 7 Nov). No machine outliner on
  macOS (nbody: 1.87 → 0.73 s). `-fno-math-errno` on Linux keeps `sqrt` inline (Graviton2: 6.29 → 4.90 s).

## Calls, inlining and stack traces

Stack traces cost nothing until an error. Every call site gets a label in a data section; on an error the runtime
walks the frame pointers and maps return addresses to sites. A function that cannot fail is inlined freely; a
function called from one place is always inlined, and its frame is restored in the trace. Only functions called
from two or more places that can fail stay out of line. The measured difference with and without trace support
stayed within noise: −1.9% to +1.6% CPU over 21 interleaved runs (M4, 5 Nov 2026, design notes).

## Dead code and what is linked

Pruning starts from the top-level statements and drops every function, method and class the program cannot
reach. A test enforces it: importing a module with 200 functions and using one costs the same as writing that one
function (within 512 bytes). Runtime parts are emitted only when used: the event loop and networking, TLS, SQLite, crypto, both regex engines,
threads, the parallel pool, `any` field tables, per-type JSON serializers, cycle visitors and the debugger agent.
The `fib` benchmark executable is 69,288 bytes (M4, 7 Nov 2026, `bench/results/2026-11-07-Apple-M4.csv`).

TLS is linked only when the program can use HTTPS: an `https://` literal, a URL computed at run time, or `listen`
with a certificate; `--tls=off` removes it. Measured on the M4 (5 Nov 2026, design notes):

| backend | protocol | handshakes/s | CPU per handshake | KB per idle connection | HTTPS executable |
|---|---|---:|---:|---:|---:|
| AWS-LC (default) | TLS 1.2, 1.3 | 6,320 | 146 µs | 6.0 | 2.3 MB |
| BearSSL (`--tls=bearssl`) | TLS 1.2 | 5,468 | 194 µs | 22.9 | 266 KB |
| none (only `http://` URLs) | — | — | — | — | 53 KB |

## Closures, async, threads and regular expressions

- **Closures** are refcounted objects whose first field is the code pointer. Captured variables are copied by
  value. A captured variable that is reassigned moves to a heap cell: one allocation and counting on every access.
- **Promises.** `await f()` and a bare `f();` call the function directly; no promise exists. A promise is built
  only when the program keeps one. `await` on a direct call costs 0.7–0.9 ns; a kept promise costs about 73 ns
  (Node: about 20 ns for both) (2 million calls, Apple M4, 12 Nov 2026).
- **Event loop.** One thread runs coroutines on kqueue (macOS), an epoll equivalent (Linux) or WSAPoll (Windows).
  A parked coroutine on the shared stack keeps only the bytes it used: 10,000 waiting tasks took 15 MB against
  Bun's 35 MB (M4, 6 Nov 2026, design notes). Why coroutines and not continuations: [Why coroutines](./coroutines.md).
- **Threads are for CPU work.** `worker_threads` runs the same program on another thread. `xisto:parallel`
  (`parallelMap`, `parallelFrom`, `parallelFill`, `parallelReduce`) runs only when the compiler proves the
  callback independent: it reads shared numbers and numeric arrays, writes nothing outside itself and prints
  nothing. The main thread then replays the loop with the computed values, so order and errors match the
  sequential run. Otherwise it runs as a plain loop with a warning. For I/O, one thread wins: see
  [HTTP benchmarks](../benchmarks/http.md#threads).
- **Regular expressions** use a linear engine (lazy DFA, Pike VM for captures) that cannot backtrack. A pattern
  with backreferences or lookaround, or one built at run time, links a backtracking engine that stops with a
  `RangeError` after 50 million steps.

## See also

- [Best practices](./best-practices.md)
- [Measuring a program](./measuring.md)
- [`any`](/docs/language/any) and [Divergences from TypeScript](/docs/language/divergences)
- [Benchmarks](../benchmarks/index.md)
