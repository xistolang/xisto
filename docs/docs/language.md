---
title: The language
---

# The language

xisto compiles TypeScript as written in typed codebases. What it asks in exchange for native speed are a few rules,
below. Diagnostics explain the rule and the fix when code breaks one.

## Types are real

- **`int` and `number` are distinct at run time.** `int` holds whole numbers up to ±2^53−1, and overflow is an
  error, not a silent float. An `int` mixed with a `number` gives a `number`.
- **Objects have a fixed layout.** `object` holds JSON.
- **Generics are compiled per use.**
- **Structural copies are copies.** A class object passed where a data shape goes is copied field by field, so
  writing through the parameter does not change the caller's object. An interface with methods is nominal.

## `null` is the only empty value

There is no `undefined` value. `x?: T`, `T | undefined`, `?.`, `??` and `??=` all work on `T | null`; write
`null` and test with `x == null`. An absent optional field still has its own state: it is omitted from JSON and
printed as `undefined` by `console.log`, as on Node. Writing `undefined` is an error.

## `==` never coerces

`==` and `!=` mean `===` and `!==` when both sides have the same type; `x == null` is true for null or absent.
Comparisons that would coerce are refused.

## `any` is a checked dynamic path

`any` works: field access by name, `x[k]`, assignment, `k in x`, arithmetic, `===`, truthiness, `??`, `?.`,
template strings, JSON and `console.log`. Each value is checked when it meets a typed slot. A field the class does
not declare is a `TypeError` (where JavaScript would read `undefined`). Calling a method on an `any` is refused
(XS2017): give the value its type.

The compiler warns where `any` is used (XS2007), because it is roughly 10× slower than the typed path.

## Promises

`await f()` and a bare `f();` are direct calls: a bare `f();` runs to the end before the next line (Node runs it
to its first `await`). A promise exists only where it is kept: `const p = f()`, `Promise<T>` fields, parameters
and arrays, `.then` / `.catch` / `.finally`, `new Promise`, `Promise.resolve` / `Promise.reject`.

## Memory

Memory is reference-counted by the compiler. The test suite requires zero
live objects at exit.

## Rule warnings

Some rule warnings compile: `undefined` read as `null`, a comparison with something that is never null, `int`
mixed with `number`, an uninferred type parameter, an `int` used as a condition, `s + x` (XS2101–XS2106).
`xisto build` prints their count, `--warnings` lists each one, and `--deny-warnings` makes them errors (for CI).

## Supported forms

All behave as on Node (the repository's `tests/ok/` has a program for each):

- **Types:** inline object types; `Record<K, V>` (index, `in`, `delete`, `for…in`, `Object.keys/values/entries`,
  `Object.fromEntries`); `A & B`; `Omit`, `Pick`, `Partial`, `Required`, `NonNullable`, `Readonly`,
  `ReadonlyArray`, `Awaited`; `ReturnType<typeof f>`; `T['field']`, `T[number]`, `typeof x`, `keyof T`;
  conditional aliases (true branch); literal types (`'a' | 'b'`, `1 | 2`); `x is T` predicates; `satisfies`;
  `as const`; `symbol` and `unique symbol`.
- **Inference:** return types (from the `return`s; recursion needs a written type), field types from the value,
  object literals without a declared type, `new Set(xs)` / `new Map(entries)` without type arguments, a generic's
  `T` from the target (`const rows: Row[] = await ds.query(sql)`).
- **Classes:** `class X extends Error` with fields and `instanceof` in `catch`; `new Error(m, { cause })`;
  interfaces with methods; `x!: T`; `let x: T;` assigned later; decorators.
- **Modules:** `import type`, `import { type X, y }`, re-exports, barrels, `export *`, `import * as ns`, a
  top-level `const ns = require('m')`, an `import` after declarations.
- **Statements and expressions:** flow narrowing as in TypeScript (`a?.f === 'x'`, assignments,
  `instanceof` / `typeof`, `Array.isArray`); IIFEs and `f(a)(b)`; labels; `do…while`; `String.raw`; rest
  parameters and spreads in calls; nested destructuring and swaps; `void expr`; `x.m?.()`; module-level generic
  arrows; default parameters.

## Known gaps

Not yet supported (rewrite the code meanwhile, or better, help fix the compiler):

- `import 'm';` for side effects is skipped (its top-level code does not run); `require()` inside a function or a
  `try`; `export * as ns` and renamed re-exports; two imports with the same name from different packages.
- Object rest (`const { a, ...rest } = o`); generic arrows inside functions (hoist them to the module); type-level
  indexing such as `Parameters<typeof f>[0][number]` or `T[keyof T]`; mapped types; `DeepPartial<T>`; generic
  interfaces with methods; a return type of `: this`; overloads.
- `Promise.allSettled`.
- `Partial<T>` has no run-time presence (absent is null): to iterate over "the keys given", use
  `Record<string, unknown>`.
- On a plain `number`: `if (n)` (compare with 0), `x.padStart(n)` with `n: number`.
- An object literal cannot implement an interface method: use a class, or function-typed fields.
- Union narrowing works on identifiers only: put `t[0].ok` in a `const` first.
- `type X = ImportedClass` is a distinct type: use the class name.
- A `const` read by a closure in its own initializer needs its type written.
