---
title: null and absent values
sidebar_position: 3
---

# null and absent values

xisto has one empty value: `null`. `T | null` is the nullable type. There is no separate `undefined` value: where
code writes `undefined`, the compiler reads `null` and warns.

```text
x.ts:1:29: warning[XS2101]: 'undefined' is deprecated in xisto: it is read as null
     1 | let s: string | undefined = undefined;
  help: write null: xisto has one empty value
```

`T | undefined` and `x?: T` are accepted and mean `T | null`. `null` alone is not a type (XS2008).

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
`x == null`, which is the same on both.

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
`undefined`); for `int` values it is a run-time error ("Map.get: key not found (check with has() first)").

## Comparing with `null`

Comparing a value that is never null with `null` compiles with warning XS2102 ("int is never null: this comparison
is always false"). Ordering comparisons (`<`, `>`) on `int | null` are refused: narrow first.

## See also

- [Equality and truthiness](./equality-and-truthiness.md)
- [Warnings](./warnings.md)
- [Divergences from TypeScript](./divergences.md)
