---
title: Best practices and what not to do
sidebar_position: 2
---

# Best practices and what not to do

Most typed TypeScript compiles to fast code as written. The patterns below are the exceptions: each one compiles,
or fails with a clear diagnostic, but costs more than it looks. Warnings are counted on every build; `--warnings`
lists them, and `--deny-warnings` turns them into errors for CI. [The performance model](./model.md) explains the
mechanisms behind each row.

## Types and values

| pattern | what happens | do this instead |
|---|---|---|
| `any`, `as any`, `(x as any)[k]` | Dynamic path, warning XS2007. A field read costs about 9 ns against 0.4 ns typed (10 million reads, Apple M4, 12 Nov 2026). An object created as `any` is a `Record`, larger than a struct. | Declare an interface, a class or `Record<string, T>`. See [`any`](/docs/language/any). |
| `undefined` | Read as `null`, warning XS2101. A comparison of a never-null value with `null` is always the same: XS2102. | Write `null`; test with `x == null`. |
| `a == b` where JavaScript would convert (int and string, say) | Error XS8148. | Convert explicitly: `Number(s) === n` or `String(n) === s`. |
| `if (n)` with an `int` | Read as `n !== 0`, warning XS2105. A `number` as a condition is accepted only with `--tsc-checked`. | Compare explicitly: `n !== 0`, `n > 0`. |
| `int` mixed with `number` | The result is `number`, warning XS2103. The loop can leave integer registers. | Keep counters and indices `int`; convert once, where you mean it. |
| A class object where a data shape (`{ k?: T }`) goes | A field-by-field copy: writes through the parameter do not reach the caller; `===` and `instanceof` differ. | Use the class type in the signature. |
| A class passed where an interface with methods goes, without `implements` | An adapter object per conversion; `===` with the original is false. | Declare `class C implements I`: no adapter. |
| `Record<string, number>` where `Record<string, unknown>` goes; `T[]` where `U[]` goes | A copy, made only when the callee provably only reads; otherwise error XS8018 asks for the type. | Declare the wider type at the source, or make the callee generic. |
| `structuredClone(x)` | A deep copy by run-time class; the copy stays an instance of the class (Node returns a plain object). A function or a cycle throws `DataCloneError`. | Copy explicitly when the structure is known. |

## Functions and generics

| pattern | what happens | do this instead |
|---|---|---|
| A generic arrow value `const f = <T>(x: T[]): T => ...` returning objects | `T` is read as `unknown`; the result is copied through JSON on every call, warning XS2110. The cost grows with the value. | Write `function f<T>(...)` or a generic method: compiled per type, no cost. |
| Generic functions over mapped tuples (`{ [K in keyof T]: ... }`) | One box per item and a checked conversion per read. Not yet measured. | Use homogeneous `Promise.all` over one type where possible. |
| A closure that reassigns a captured variable | The variable moves to a heap cell: an allocation, and counting on every access. | Capture a `const`; return the new value instead of assigning it. |
| Patching a prototype (`Object.getPrototypeOf(x).m = ...`) | Every reachable class with that method calls it through a slot: an indirect call. | Use a subclass or a wrapper function. |
| `x = x.with(v)` (immutable update returning `new C(...)`) | When `x` holds the only reference, the fields are rewritten in place: no allocation. | Keep this style; avoid keeping a second reference to the old object in hot loops. |

## Async, I/O and memory

| pattern | what happens | do this instead |
|---|---|---|
| Storing a promise, or `.then` chains | A coroutine and a promise: about 73 ns per kept promise against 0.7–0.9 ns for `await f()` (2 million calls, Apple M4, 12 Nov 2026; Node: about 20 ns for both). | `await` the call directly when you do not need the promise. |
| A bare `f();` of an async function, as fire-and-forget | It is a direct call: `f` runs to completion before the next line, unlike Node. | Keep the promise (`const p = f();`) when it must run in the background. |
| `readFileSync` of a large file | The whole file in memory: 977.8 MB peak RSS for a 1 GB log against 1.3 MB streaming (M4, 5 Nov 2026, `bench/results/2026-11-05-Apple-M4-streams.csv`). | Read it as a stream, or by parts with `readStdinParts` from `xisto:io`. |
| An allocation loop with `await`, `yield`, closures, unions, casts or `unknown` inside | No region: every object is counted and freed one by one. A `try` anywhere in the program rules out dual regions; a `try` in a function rules out stack allocation there. | Keep allocation-heavy loops plain; move the `await` or the closure out. Check with `XISTO_REGION_WHY=1`. |
| `parallelMap` / `parallelFrom` / `parallelFill` / `parallelReduce` with a callback that prints, calls a function value, uses timers or writes outside itself | The proof fails: a plain sequential loop and a warning saying why. | Callbacks may only read shared numbers, booleans and numeric arrays. |

## Arrays and builds

| pattern | what happens | do this instead |
|---|---|---|
| `nums.sort()` on numbers | Error XS7089: JavaScript would compare as strings. `sort()` on strings works as in JavaScript. | `nums.sort((a, b) => a - b)`. |
| `xs.push(a, b)` | On a variable: one `push` per argument. On an expression that cannot be read twice (a call result): XS7083. | Push to a variable. |
| `xs.unshift(a, b)` | Error XS7117: `unshift` takes one argument. | One `unshift` per value, last first. |
| Measuring or serving with `--dev` | `-O0`: 4–10× slower than release (fib 0.11 vs 0.02 s, nbody 3.78 vs 0.39 s; M4, 7 Nov 2026, `bench/results/2026-11-07-Apple-M4.csv`). Frames over 16 KB can overflow a server's 64 KB request stack (SIGBUS). | `--dev` for the edit loop only; release (the default) for anything you measure or deploy. |
| Leaks you do not see | Reference counting cannot leak silently, except through cycles of types the compiler did not mark. | Run with `XISTO_RC_STATS=1`: the line must say `0 still alive at exit`. `xisto test` requires it. |

## How to find what is slow

Measure first. Build with `--profile` to find the function, use `XISTO_RC_STATS=1` to count allocations, read
`GET /__metrics` on a server, and ask the compiler why an optimization did not apply with the `XISTO_*_WHY`
knobs. [Measuring a program](./measuring.md) has the workflow.

## See also

- [The performance model](./model.md)
- [Measuring a program](./measuring.md)
- [`any`](/docs/language/any) and [Divergences from TypeScript](/docs/language/divergences)
