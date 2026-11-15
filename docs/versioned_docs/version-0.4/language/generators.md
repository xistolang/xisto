---
title: Generators
sidebar_position: 8
---

# Generators

`function*` declares a generator. Its type is `Generator<T>`. Generators are consumed with `for…of`.

```ts
function* range(from: int, to: int): Generator<int> {
  for (let i = from; i < to; i++) yield i;
}
function* evens(src: Generator<int>): Generator<int> {
  for (const x of src) if (x % 2 === 0) yield x;
}
let total = 0;
for (const x of evens(range(0, 10))) total += x;
console.log(total);   // 20
```

Infinite generators with `break`, generators stored in variables and in arrays, and nested `for…of` over them work
as on Node:

```ts
function* fib(): Generator<int> { let a = 0, b = 1; while (true) { yield a; const t = a + b; a = b; b = t; } }
const out: int[] = [];
for (const f of fib()) { if (f > 50) break; out.push(f); }
```

Async generators (`async function*`, `AsyncGenerator<T>`) are consumed with `for await…of`.

## How they compile

| Use | Compiled as |
|---|---|
| a generator consumed by one `for…of` at the call | inlined into the loop: no generator object at all |
| a simple generator kept as a value | a state machine |
| other generators | a coroutine with its own stack |

The inlined form is the fast one. A generator passed around as a value pays for its state or stack.

## Limits

- `yield` inside a `try` that has a `finally` is refused:

```text
error[XS6011]: 'yield' inside a try with finally is not supported yet
```

- `.next()` is not available: iterate with `for…of`.
- A generator cannot be spread (`[...g]`) or passed to `Array.from`. Collect with a loop and `push`.

## See also

- [Functions and async](./functions-async.md)
- [Known limitations](./known-limitations.md)
