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

## Methods as values: bind, call, apply

A method read without a call, `obj.method`, is a function bound to `obj`. `bind`, `call` and `apply` work on
methods and on function values; `bind` takes partial arguments and gives a function of the parameters left.

```ts
class Feed {
  name = "feed";
  got: string[] = [];
  onData(chunk: string): void { this.got.push(this.name + ":" + chunk); }
  fmt(n: number): string { return this.name + "#" + n; }
  static create(): Feed { return new Feed(); }
}
const f = new Feed();
const h = f.onData.bind(f);
emitter.on("data", h);
emitter.off("data", h);                    // the same function: removed
[1, 2].map(f.fmt.bind(f));                 // [ 'feed#1', 'feed#2' ]
[1, 2].map(f.fmt, f);                      // the same (thisArg is the method's own object)
const add3 = (a: number, b: number, c: number): number => a + b + c;
add3.bind(null, 1, 2)(3);                  // 6
f.fmt.call(f, 7); add3.apply(null, [1, 2, 3]);
const make = Feed.create;                  // a static method as a value
console.log(add3.length, f.fmt.name);      // 3 fmt
```

What differs from JavaScript:

- `const g = obj.method` is bound to `obj`. JavaScript reads it unbound: calling `g()` alone loses `this` (a
  `TypeError` in a class). A program that works in JavaScript does the same here; one that would lose `this`
  keeps it.
- Each read of `obj.method` makes a new function, so `obj.method === obj.method` is `false` (JavaScript: `true`).
  Keep the value in a variable or a field when you need the same function twice (`on`/`off`). Two `bind` calls
  give two functions, as in JavaScript.
- `a.m.bind(b)` and `a.m.call(b, ...)` run `b`'s `m` (`b`'s override, if its class has one). JavaScript runs the
  `m` read from `a` with `this = b`. `thisArg` must be an object of the method's class.
- On a function value or an arrow, `thisArg` of `bind`, `call` and `apply` is not used: arrows have no `this` of
  their own, and xisto passes none to functions. `apply` takes an array literal or the array of a rest parameter.
- `xs.map(fn, thisArg)` (and `forEach`, `filter`, `find`, `findIndex`, `some`, `every`) accepts a `thisArg` only
  when it is the object of the method passed (`this.fmt, this`) or when `fn` is an arrow.
- `f.length` counts the declared parameters (not a rest parameter); `f.name` is known for a top-level function,
  a static method and `obj.method`. For a function held in a variable or a parameter it is an error (XS8013).
- A generic method is not a value (XS8049): wrap it, `(x: T) => obj.m(x)`.

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
