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
| `undefined`, `void 0` | a value | a deprecated synonym of `null`, warning XS2101 | one empty value |
| `x === undefined` with `x` holding `null`; `typeof x === "undefined"` | `false` | `true`: read as `x == null` | `undefined` and `null` are one |
| a variable that held `undefined`: `console.log(v)`, `typeof v` | `undefined`, `"undefined"` | `null`, `"object"` | it holds `null` |
| `[1, undefined]` printed | `[ 1, undefined ]` | `[ 1, null ]` | (`JSON.stringify` is `[1,null]` on both) |
| optional field set to `undefined`: `Object.keys`, `console.log` | key listed, `nick: undefined` | left out | the field is absent; `JSON.stringify` leaves it out on both |
| optional field set to `null`, declared without `null` (`nick?: string`): `JSON.stringify`, `Object.keys`, `console.log` | `"nick":null`, key listed | left out | such a field holds absent and `null` as one value; declare `nick?: string \| null` to keep the `null` |
| absent optional field `=== null` | `false` | `true` | absent reads as `null`; use `== null` |
| `Map.get` of a missing key | `undefined` | `null`; with an `int`, `number` or `boolean` value that is so wherever the result can be absent: `m.get(k) ?? d`, `m.get(k) \|\| d`, `m.get(k) === undefined`, a condition, `const v = m.get(k)` (`v` is `T \| null`) and a `T \| undefined` return, argument or field; where only a `T` fits (`m.get(k) + 1`) it is a run-time error (check with `has()` first) | no `undefined` |
| field missing on an `any` | `undefined` | `null` | |
| `x!`, `x as T`, `base as Sub` | trusted | checked: `TypeError` | nothing silent |
| `x instanceof C`, `x: object` | `true` for a `C` | error XS8150 | an `object` holds JSON data, not the instance: declare `C`, a union or `unknown` |
| `Sub.x = v` (or `this.x = v` in a static method `Sub` inherits), `x` a static field of a base | `Sub` gets a property of its own | error XS8151 | one variable per static: write it through the class that declares it |
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
| `m.keys()`, `m.values()` | iterators | arrays (`take`, `drop`, `toArray` make them lazy iterators) | |
| `xs.at(99)`, `xs.findLast(f)` with no match | `undefined` | `null` | one empty value |
| `Object.freeze(o)`, then `o.x = 1` | `TypeError` (strict mode) | the write happens; `Object.isFrozen` is refused | no flag per object |
| `console.log(new AggregateError(es, m))` | the stack, then `[errors]` | `AggregateError: m {` `[errors]: ... }` | xisto prints no frames |
| `m.set(new, …)` inside `for…of` over `m` | the loop sees it | the loop does not see it | |
| `const a = []` | `any[]` | error XS3008 | a type is needed |
| `xs.length = 5` on a 3-element array | grows it (holes up to index 4) | error XS6052 when the length is known there (`[1, 2]`, `new Array(3).fill(0)` just before); else an error at run time (`RangeError: xisto cannot make an array longer...`) | an array has no holes: add with `push(...)`, or make it `new Array(n).fill(v)` |
| `new Date("bad").getFullYear()` | `NaN` | `0` | getters return `int` |
| `Date.toString()` zone name | `(Brasilia Standard Time)` | `(-03)` | no ICU data |
| `new Intl.NumberFormat(...)`, `d.toLocaleDateString("pt-BR")` | built in | error XS5005 (`Intl`) or XS7014 until `xisto add intl` | `Intl` is the `intl` package, en-US and pt-BR |
| `JSON.parse(t) as T` with a `Date` field | the field stays a string | a `Date` | the declared type is decoded |
| `execSync(cmd)` without `encoding` | `Buffer` | `string` | |

## Functions, modules, classes

| What | Node | xisto | Why |
|---|---|---|---|
| function that can reach its end without `return` | allowed | error XS8058 | |
| `throw "text"` | allowed | error XS8005 | only objects can be thrown |
| `const g = obj.method; g()` | `this` is lost (`TypeError`) | bound to `obj` | correct programs run the same |
| `obj.method === obj.method` | `true` | `false` (a new bound function per read) | keep it in a variable for `on`/`off` |
| `a.m.bind(b)`, `a.m.call(b)` | `a`'s `m` with `this = b` | `b`'s `m` | dispatch on the object it runs on |
| `JSON.stringify(x, replacer)` | the replacer sees class instances | it sees them as plain objects | the value is rewritten from its JSON |
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
`extends Error`, `Promise.all` of different types (a tuple), `Buffer`, `string[].sort()`, `process.env.X`, `sort()` and `toSorted()` without a comparator on numbers, bigints,
booleans and `unknown[]` (the elements' `String()` forms by UTF-16 code units, as on Node: `[1, 10, 2, 3]`),
`Object.assign({}, a)`, `Promise.reject(e)` beside other promises in `Promise.all`/`race`/`any`/`allSettled`.

## See also

- [Known limitations](./known-limitations.md)
- [Warnings](./warnings.md)
- [Node compatibility](/docs/guides/node-compatibility)
