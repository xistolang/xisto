---
title: null and absent values
sidebar_position: 3
---

# null and absent values

xisto has one empty value: `null`. `T | null` is the nullable type. There is no separate `undefined` value:
`undefined` is accepted as a deprecated synonym of `null`. Where code writes it, the compiler reads `null` and warns
(XS2101).

```text
x.ts:1:29: warning[XS2101]: 'undefined' is deprecated in xisto: it is read as null
     1 | let s: string | undefined = undefined;
  help: write null: xisto has one empty value
```

`T | undefined` and `x?: T` are accepted and mean `T | null`. `null` alone is not a type (XS2008).

## `undefined` as a value

Every form compiles, each with one XS2101 warning at the place it is written:

| Written | Read as |
|---|---|
| `undefined`, `void 0` (any literal after `void`) | `null` |
| `x === undefined`, `x == undefined` | `x == null` |
| `x !== undefined`, `x != undefined` | `x != null` |
| `typeof x === "undefined"` (`!==`), with `x` a `T \| null` or `unknown` | `x == null` (`x != null`) |
| `return undefined;` in a `void` function | `return;` |

`typeof x === "undefined"` with `x` never null stays the typeof test: always `false`, as on Node, without a warning.

On an optional field, `undefined` means absent, not `null`: `o.nick = undefined` and `{ nick: undefined }` leave the
field out, so `JSON.stringify` leaves it out as Node does, and `o.nick === undefined` is `true`.

What stays visible, because the value is `null` (see [Divergences](./divergences.md)):

- `console.log(v)` of a variable that held `undefined` prints `null`, and `typeof v` is `"object"`.
- `x === undefined` is `true` for an `x` that holds `null` (Node: `false`); `typeof x === "undefined"` too.
- `[1, undefined]` prints `[ 1, null ]` (Node: `[ 1, undefined ]`); `JSON.stringify` gives `[1,null]` on both.
- An optional field set to `undefined` is absent: `Object.keys` and `console.log` leave it out, where Node lists
  the key and prints `nick: undefined`.

## Why

Two empty values cost a check per read and a representation per type. Real code rarely means two different
things by them. A nullable `int`, `number` or `boolean` uses a sentinel value inside the same 8 bytes, so
`int | null` costs nothing extra.

## Optional fields and "absent"

An optional field that was never given is absent. It is left out of `console.log` and JSON, and `in` is false, as
on Node:

```ts
interface User { name: string; nick?: string; age?: int; admin?: boolean }
const a: User = { name: "ana" };
console.log(a, JSON.stringify(a));        // { name: 'ana' } {"name":"ana"}
console.log("nick" in a);                 // false
console.log(a.nick ?? "(none)", a.nick?.length);   // (none) undefined
```

An absent field reads as `null`. So `a.admin === null` is `true` in xisto and `false` on Node. Test emptiness with
`x == null`, which is the same on both. `typeof a.nick` is `"undefined"`, as on Node.

An optional field declared with `null` in its type (`nick?: string | null`) tells a `null` given from absent, as
Node does: set to `null` (`u.nick = null`, `{ nick: null }`, or read by `JSON.parse` from `"nick":null`), it is
written `"nick":null` by `JSON.stringify` and `nick: null` by `console.log`, listed by `Object.keys`, and `"nick" in
o` is `true`; never set, or set to `undefined`, it is left out. Reading it gives `null` either way. The flag costs one byte in the objects of such a class, and nothing
in a class without such a field. An optional field without `null` in its type (`nick?: string`) holds absent and
`null` as one value, left out of the JSON. To leave out every `null` of the JSON a program writes, see
[JSON and nulls](../guides/node-compatibility.md#json-and-nulls).

## `?.`, `??`, `??=`

All three work on `T | null`, including nullable primitives:

```ts
let c: string | null = null;
c ??= "set";
const n = a.age ?? 0;             // int
const f = a.admin ?? false;       // boolean
```

Both sides of `??` must have the same type: `a.admin ?? "none"` is an error.

## Narrowing

A nullable value is narrowed by `!==`/`===` against `null`, `==`/`!=`, `&&`, `||` and `!`, as in TypeScript:

```ts
function len(s: string | null): int { if (s === null) return 0; return s.length; }
let cur = head;
while (cur !== null) { total += cur.value; cur = cur.next; }
if (box.inner && box.inner.label !== null) console.log(box.inner.label.length);
```

Field paths are narrowed inside the block. An assignment undoes the narrowing. A variable that a closure captures
and reassigns lives in a heap cell; inside a function it is narrowed too, and each read after the test is checked
at run time:

```text
runtime error: TypeError: 'last' is null here: a call made since it was checked set it back to null
```

`x!` and `(x as T)` are checked: `null` there is a `TypeError`, where TypeScript would go on with `null`.

## What prints `undefined`

`console.log` prints `undefined` where Node would: an absent optional field read through `?.`, an unmatched regex
group. A `null` you stored prints `null`. `Map.get` of a missing key returns `null` for reference values (Node:
`undefined`); for `int`, `number` and `boolean` values too wherever the result can be absent (`m.get(k) ?? 0`,
`m.get(k) === undefined`, `const v = m.get(k)`, a `T | undefined` slot). Read where only the value's type fits
(`m.get(k) + 1`), a missing key is a run-time error ("Map.get: key not found (check with has() first)").

## Comparing with `null`

Comparing a value that is never null with `null` compiles with warning XS2102 ("int is never null: this comparison
is always false"). Ordering comparisons (`<`, `>`) on `int | null` are refused: narrow first.

## See also

- [Equality and truthiness](./equality-and-truthiness.md)
- [Warnings](./warnings.md)
- [Divergences from TypeScript](./divergences.md)
