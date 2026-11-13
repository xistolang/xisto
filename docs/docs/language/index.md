---
title: The language
slug: /language
sidebar_position: 1
---

# The language

xisto is a strict subset of TypeScript. It reads TypeScript syntax and gives it static semantics: every value has
a type the compiler knows, and that type decides the machine code. What it asks in exchange for native speed is a
small set of rules. This section states them, with the reason for each.

## Principles

- **TypeScript syntax, static semantics.** No new syntax. The type `int` is the one addition; Node strips type
  annotations, so on Node an `int` is simply a `number`.
- **Valid xisto is valid TypeScript.** A program that compiles with xisto runs on Node with the same output. The
  repository's test suite checks this for every program in `tests/ok/`: Node is the oracle.
- **Nothing implicit.** Conversions that JavaScript does silently (`1 == "1"`, `if (n)` on a `number`) are either
  refused or compiled with a warning that says what happens.
- **Good errors.** Every diagnostic has a stable code, the place, the rule and a fix:

```text
x.ts:3:16: error[XS8082]: array index must be int, but this is number
     3 | console.log(xs[k]);
       |                ^
  help: declare the index as int, or convert it with Math.trunc(x)
```

- **Divergences are deliberate.** Where xisto behaves differently from Node, it is on purpose and listed in
  [Divergences from TypeScript](./divergences.md).

## What changes for you

Most typed TypeScript compiles as written. The rules you meet first:

| Rule | Page |
|---|---|
| `int` and `number` are different types | [Numbers](./types.md) |
| `null` is the only empty value; `undefined` reads as `null` | [null and absent values](./null.md) |
| `==` never converts; numbers are not conditions | [Equality and truthiness](./equality-and-truthiness.md) |
| `any` works, but takes a slower, checked path | [any](./any.md) |
| generics are compiled once per type | [Classes, interfaces and generics](./classes-interfaces-generics.md) |
| `await f()` is a direct call | [Functions and async](./functions-async.md) |

## Map of this section

- [Numbers](./types.md): `int`, `number`, literals, overflow, bitwise, `bigint`.
- [null and absent values](./null.md): optional fields, `?.`, `??`, narrowing.
- [Equality and truthiness](./equality-and-truthiness.md): `==`, conditions, `&&` and `||`.
- [any](./any.md): the checked dynamic path and its cost.
- [Classes, interfaces and generics](./classes-interfaces-generics.md): dispatch, adapters, unions, tuples.
- [Functions and async](./functions-async.md): closures, promises, timers.
- [Generators](./generators.md): `function*` and `for…of`.
- [Modules](./modules.md): imports, barrels, `xisto:` and `node:`, packages.
- [Errors](./errors.md): `throw`, `try`, stack traces.
- [Decorators](./decorators.md): static reflection and method decorators.
- [Divergences from TypeScript](./divergences.md): the full table.
- [Warnings](./warnings.md): rule warnings and `--deny-warnings`.
- [Known limitations](./known-limitations.md): what is not supported yet.

For the performance side of these rules, see [Best practices](/docs/performance/best-practices).
