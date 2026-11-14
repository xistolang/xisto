---
title: Divergences from TypeScript
sidebar_position: 12
---

# Divergences from TypeScript

Every row was checked on 12 Nov 2026 by running the same program with xisto and with Node 26 (type stripping).
"Error" means a compile error with the code shown, unless it says run-time.

## Numbers

| What | Node | xisto | Why |
|---|---|---|---|
| `int` | does not exist (annotations are stripped) | a 64-bit integer type | indices and counters without conversion |
| `int / int` | `number` | `number` (`7 / 2` is `3.5`) | same, on purpose |
| `int` leaving ±(2^53−1), including `**` | loses precision | run-time error | nothing silent |
| literal above 2^53−1 | rounded | rounded the same way | `int` is JavaScript's safe integer |
| `a[i]` with `i: number` | accepted | error XS8082 | an index is an `int` |
| `-0` in a reassigned `int` variable or `int[]` element | `-0` | `0` | kept elsewhere |
| `5 & true` | converts | error XS8115 | no conversions |

## Empty values

| What | Node | xisto | Why |
|---|---|---|---|
| `undefined` | a value | read as `null`, warning XS2101 | one empty value |
| absent optional field `=== null` | `false` | `true` | absent reads as `null`; use `== null` |
| `Map.get` of a missing key | `undefined` | `null`; with an `int`, `number` or `boolean` value that is so for `m.get(k) ?? d` and `const v = m.get(k)` (`v` is `T \| null`), any other use of the result is a run-time error (check with `has()` first) | no `undefined` |
| field missing on an `any` | `undefined` | `null` | |
| `x!`, `x as T`, `base as Sub` | trusted | checked: `TypeError` | nothing silent |
| `arr[i]`, `bytes[i]` out of bounds | `undefined` | run-time error (catchable) | |

## Conditions and operators

| What | Node | xisto | Why |
|---|---|---|---|
| `1 == "1"` | `true` | error XS8148 | `==` is `===` when nothing converts |
| `if (n)`, `n: number` | truthiness | error XS8022 (accepted with `--tsc-checked`) | `NaN` hides there |
| `if (n)`, `n: int` | truthiness | `n !== 0`, warning XS2105 | |
| `s && x` with non-booleans | one of the operands | a `boolean` | |
| `"n=" + n` | text | text, warning XS2106 | |

## Strings, collections, dates

| What | Node | xisto | Why |
|---|---|---|---|
| `s.length`, `slice`, `indexOf` | UTF-16 units | code points (`"héllo👋".length` is 6, Node 7) | UTF-8 strings |
| `s[i]` | a character | error XS6033 | use `s.slice(i, i + 1)` |
| `s.match("text")` | converts to a RegExp | error XS7096 | no implicit conversion |
| `s.normalize("NFD")` | decomposes | returns the string unchanged | no Unicode tables |
| `m.keys()`, `m.values()` | iterators | arrays | |
| `m.set(new, …)` inside `for…of` over `m` | the loop sees it | the loop does not see it | |
| `numbers.sort()` without comparator | sorts as strings | error XS7089 (strings sort as on Node) | the default surprises |
| `const a = []` | `any[]` | error XS3008 | a type is needed |
| `new Date("bad").getFullYear()` | `NaN` | `0` | getters return `int` |
| `Date.toString()` zone name | `(Brasilia Standard Time)` | `(-03)` | no ICU data |
| `JSON.parse(t) as T` with a `Date` field | the field stays a string | a `Date` | the declared type is decoded |
| `execSync(cmd)` without `encoding` | `Buffer` | `string` | |

## Functions, modules, classes

| What | Node | xisto | Why |
|---|---|---|---|
| bare `f();` of an async function | runs to its first `await` | runs to the end | no promise unless kept |
| function that can reach its end without `return` | allowed | error XS8058 | |
| `throw "text"` | allowed | error XS8005 | only objects can be thrown |
| `catch (e)` | `unknown` | `Error` | |
| class object passed as a data interface | the same object | a copy | fixed layouts |
| class object passed as a method interface without `implements` | the same object | an adapter: `===` differs | |
| `yield` inside `try … finally` | allowed | error XS6011 | |
| `import "./x.ts"` | runs the module | does not run its top level | |
| `import … from "node:fs"` | Node | error XS4001 unless `--node-compat` | it is not Node |
| top-level value read across a circular import too early | `ReferenceError` | reads it | |

## No longer divergent

Earlier versions refused these; they now behave as on Node: `==` without conversion, `int` mixed with `number`
(warning XS2103), optional `boolean`/`int` fields, `(int | null)[]`, regex backreferences and lookaround,
`extends Error`, `Promise.all` of different types (a tuple), `Buffer`, `string[].sort()`, `process.env.X`.

## See also

- [Known limitations](./known-limitations.md)
- [Warnings](./warnings.md)
- [Node compatibility](/docs/guides/node-compatibility)
