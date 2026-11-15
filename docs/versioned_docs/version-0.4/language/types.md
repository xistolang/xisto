---
title: Numbers
sidebar_position: 2
---

# Numbers

xisto has two number types. `number` is a 64-bit IEEE double, as in JavaScript. `int` is a whole number held in a
64-bit integer register. Indices, lengths and counters are `int`; that is where most of the speed comes from.

## `int` and `number`

| Expression | Type | Value |
|---|---|---|
| `const a: int = 7` | `int` | 7 |
| `a / 2` | `number` | 3.5, as in JavaScript |
| `a % 2`, `a * 2`, `a - 1` | `int` | |
| `Math.trunc(x)`, `Math.floor(x)`, `Math.round(x)` | `int` | |
| `a + 1.5` | `number` | with warning XS2103 |
| `"n=" + a` | `string` | with warning XS2106 |

An `int` mixed with a `number` gives a `number`, and the compiler says so:

```text
x.ts:9:13: warning[XS2103]: int mixed with number in arithmetic: the result is number
  help: declare the type you mean: Number(i) for a number, Math.trunc(x) for an int
```

A `number` where an `int` goes is an error. Convert explicitly:

```text
error[XS8018]: return value of 'fact' must be int, but this is number
  help: convert explicitly: Math.trunc(x), Math.floor(x), Math.ceil(x) or Math.round(x)
```

## Literals and inference

An integer literal takes the type its context asks for. Without a context it is a `number`, as TypeScript infers.
A literal above 2^53−1 is a `number` and is rounded exactly as Node rounds it (`9007199254740993` prints
`9007199254740992`).

A `let` without a type, started with an integer literal, is an `int` when every assignment to it is an integer by
its form: `i++`, `i--`, `i += 1`, `i = i + k`, `i = Math.floor(x)`, `i = xs.length`, with `k` itself an int.
Anything else leaves it a `number`:

```ts
function b(n: int): int { let t = 0; for (let i = 0; i < n; i++) t += i; return t; }   // t is int
function c(n: int): int { let r = 1; for (let i = 2; i <= n; i++) r *= i; return r; }  // error: r is number
```

A product is not an integer by form (it can pass 2^53). Write `let r: int = 1` when you mean an `int`. An inferred
`int` becomes a `number` by itself where a `number` goes, so code that compiled keeps compiling.

## Range and overflow

An `int` holds JavaScript's safe integers, ±(2^53−1), so its results never differ from Node's. Leaving the range is
a run-time error, never a silent loss of precision:

```text
runtime error: integer overflow: the result left JavaScript's safe integer range (+-2^53 - 1); use number or bigint
    at fact (t5.ts:1:77)
```

`int ** int` is checked the same way. Use `number` when an approximate result is fine, `bigint` when it must be
exact.

## Indices are `int`

Array indices and `length` values are `int`. A `number` index is refused (XS8082); convert it with `Math.trunc`.
An index out of bounds is a run-time error (catchable), where JavaScript reads `undefined`.

## Bitwise operators

`&`, `|`, `^`, `~`, `<<`, `>>` and `>>>` accept `int` and `number` operands and convert them to 32 bits, as
JavaScript does: `1 << 31` is `-2147483648`, `-1 >>> 0` is `4294967295`. The result is an `int`. A compound
assignment (`h ^= c`) needs `h` declared `int` (XS8024). A `boolean` operand is refused (XS8115).

## −0

`-0` is kept where the value is seen as a number: `Math.round(-0.4)`, `0 * -1` and `const r: int = Math.round(-0.4)`
print `-0`, as on Node. It is lost in a reassigned `int` variable or an `int[]` element, which print `0`.

## `bigint`

`bigint` literals (`123n`) and arithmetic work as on Node: `2n ** 70n`, `%`, `typeof x === "bigint"`.

## See also

- [Equality and truthiness](./equality-and-truthiness.md)
- [Warnings](./warnings.md)
- [Divergences from TypeScript](./divergences.md)
