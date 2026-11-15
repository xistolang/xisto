---
title: Functions and async
sidebar_position: 7
---

# Functions and async

## Functions as values

Function declarations, arrow functions, function expressions, nested functions, default parameters, rest
parameters and spreads in calls all work. Functions are values: pass them, store them, return them.

```ts
const base = 10;
const add = (x: int): int => x + base;
const apply = (f: (x: int) => int, v: int): int => f(v);
console.log(apply(add, 5), [1, 2, 3].map((x) => x * 2));   // 15 [ 2, 4, 6 ]
```

## Closures

A closure captures by value when the variable is never reassigned after capture: a copy, no allocation. A
variable that is captured **and** reassigned lives in a heap cell shared by the function and the closure, so
JavaScript's semantics hold:

```ts
function makeCounter(): () => int { let n = 0; return () => { n++; return n; }; }
let label = "a";
const read = (): string => label;
label = "b";
console.log(read());   // b
```

A heap cell costs an allocation and reference counting. Prefer `const` copies in hot code.

## async and await

A function returning `Promise<T>` is async. Promises are created only where the program keeps one:

| Form | What happens |
|---|---|
| `await f()` | a direct call; no promise object |
| `f();` as a statement | a direct call that **runs to the end** before the next line |
| `const p = f()`, `Promise<T>` fields, parameters, arrays | a real promise; `f` runs as a task |
| `.then` / `.catch` / `.finally`, `new Promise`, `Promise.resolve` / `reject` | real promises, microtasks |

The bare `f();` is a divergence. Node runs `f` until its first `await` and goes on:

```ts
work("bare", 20);          // xisto: start bare, end bare, after bare
console.log("after bare"); // Node:  start bare, after bare, … end bare later
```

Cost, measured on an Apple M4 (12 Nov 2026), 2 million calls: `await f()` 0.7–0.9 ns per call; a kept promise
(`const p = f(); await p`) about 73 ns. Node: about 20 ns for both. Await directly where you can. How waiting
works underneath, and why: [Why coroutines](../performance/coroutines.md).

## Promise.all, race, allSettled

`Promise.all` and `Promise.race` over an array literal run each element as a task:

```ts
const [x, y] = await Promise.all([a(), b()]);                      // different types: a tuple
const first = await Promise.race([work("slow", 30), work("fast", 5)]);
const rs = await Promise.allSettled(ps);                           // ps: Promise<number>[]
for (const r of rs) console.log(r.status);                         // fulfilled / rejected
```

## Unhandled rejection

A rejection nobody handles ends the program with exit code 1 and a trace:

```text
uncaught Error (in promise): lost
    at fail (p3.ts:1:45)
    at main (p3.ts:2:50)
```

## Timers

`setTimeout`, `setInterval`, `clearInterval` and `setTimeout(ms)` from `xisto:timers/promises` work as on Node
and keep the process alive while pending. The order of output of a program with timers and `.then` matches Node.

## See also

- [Generators](./generators.md)
- [Errors](./errors.md)
- [Known limitations](./known-limitations.md)
- [Best practices](/docs/performance/best-practices)
