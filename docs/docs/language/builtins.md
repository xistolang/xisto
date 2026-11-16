---
title: Recent built-in methods
sidebar_position: 8.5
---

# Recent built-in methods (ES2022–ES2025)

The methods that recent JavaScript added to arrays, strings, `Object`, `Map`, `Set`, iterators, `Promise` and
`BigInt` compile as on Node. Most are written in xisto itself, in the module `xisto:builtins`, which is loaded only
by a program that names one of them: a program that does not use them pays nothing.

## Arrays and strings

```ts
const xs = [5, 1, 4, 2, 3];
xs.at(-1);                          // 3 (null past either end)
xs.findLast((x) => x > 3);          // 4
xs.findLastIndex((x) => x > 9);     // -1
xs.toSorted((a, b) => a - b);       // a sorted copy; xs is unchanged
xs.toReversed();                    // a reversed copy
xs.with(1, 10);                     // a copy with one element replaced
xs.toSpliced(1, 2, 7, 8);           // a copy with elements removed and inserted
[1, 2, 3, 4, 5].copyWithin(0, 3);   // [4, 5, 3, 4, 5], in place
for (const v of xs.values()) {}     // an iterator (a generator) over the elements
for (const i of xs.keys()) {}       // ... over the indices
Array.of(1, 2, 3);                  // [1, 2, 3]
Array.from(set, (x) => x * 2);      // of an array, a Set, a Map, a string or a generator
"hello".at(-1);                     // "o"
```

`xs.with(i, v)` out of range throws the `RangeError` Node throws (`Invalid index : 5`). `sort()` and `toSorted()`
without a comparator use JavaScript's default order: the elements' `String()` forms compared by UTF-16 code units
(`[3, 1, 10, 2]` gives `[1, 10, 2, 3]`), for strings, numbers, bigints, booleans and `unknown[]`; a `null` sorts as
the string `"null"` (xisto has no `undefined` to put last). Arrays of objects still need a comparator (XS7089). The
indices are
`int`, as everywhere in xisto. `s.isWellFormed()` is `true` and `s.toWellFormed()` is `s`: a string of xisto is
UTF-8 and cannot hold a lone surrogate.

## Object

```ts
Object.groupBy(items, (it) => it.kind);   // a Record<string, T[]>, printed [Object: null prototype] { ... }
Object.assign(target, a, b);              // several sources, in order; a Record into a Record too
Object.assign({}, a, b);                  // a new object: { ...a, ...b }
Object.fromEntries(map);                  // of a Map<string, V>, or of [string, V] pairs
Object.entries(new Point());              // of a class instance: its fields (inherited ones first)
Object.getOwnPropertyNames(o);            // as Object.keys(o)
Object.hasOwn(o, "k");                    // as "k" in o
Object.is(NaN, NaN);                      // true; Object.is(0, -0) is false
Object.defineProperty(rec, "k", { value: 1 });
Object.freeze(o);                         // returns o
```

- `Object.freeze`, `Object.seal` and `Object.preventExtensions` return the object unchanged. Nothing records that
  it was frozen, and a later write is not refused (Node, in a module, throws a `TypeError`). For the same
  reason `Object.isFrozen`, `isSealed` and `isExtensible` are refused (XS8015) rather than answered wrongly.
  Use `Readonly<T>` and `readonly` fields: TypeScript refuses the write before the program runs.
- `Object.defineProperty` and `defineProperties` take a descriptor written in place with a `value`: it becomes an
  assignment of that field (or of that entry of a Record). `writable` and `configurable` change nothing; `get`,
  `set` and `enumerable: false` are refused (XS8015).
- `Object.groupBy`'s keys are strings; `Map.groupBy(items, f)` keys a `Map` by what `f` returns (string or
  number keys, as every `Map` of xisto). Both take an array, a `Set` or a generator.

## Set methods (ES2025)

`union`, `intersection`, `difference`, `symmetricDifference`, `isSubsetOf`, `isSupersetOf` and
`isDisjointFrom` take another `Set` of the same type and return a new `Set`. The order of the elements is the
one the specification's steps give, as on Node (`a.intersection(b)` walks the smaller of the two). Node also takes
a "set-like" object (anything with `size`, `has` and `keys`); xisto takes a `Set` only.

## Iterator helpers (ES2025)

A generator, and the iterators of `xs.values()`, `xs.keys()`, `m.keys()`, `m.values()`, `m.entries()` and
`s.values()`, have `map`, `filter`, `take`, `drop`, `flatMap`, `forEach`, `reduce`, `some`, `every`, `find`
and `toArray`. They are lazy: an element is pulled only when the next step needs it, so they work on an
infinite generator:

```ts
function* naturals(): Generator<number> { let i = 0; while (true) yield i++; }
naturals().map((x) => x * 2).filter((x) => x % 3 === 0).take(3).toArray();   // [0, 6, 12]
naturals().drop(2).take(2).toArray();                                       // [2, 3]
naturals().find((x) => x * x > 10);                                         // 4
```

The callbacks receive the element and a counter, as on Node. `flatMap`'s callback returns an array. A `Map`'s
or a `Set`'s `keys()`, `values()` and `entries()` are arrays in xisto (see
[Divergences](./divergences.md)): `m.keys().map(f)` is that array's `map` (eager, an array), while
`take`, `drop` and `toArray` turn it into a lazy iterator first. `Iterator.from` is not there.

## Promise

```ts
const { promise, resolve, reject } = Promise.withResolvers<string>();
setTimeout(() => resolve("later"), 5);
console.log(await promise);

const first = await Promise.any([fail(5), value(30, 1), value(15, 2)]);   // 2: the first to fulfil
```

- `Promise.withResolvers` needs its type argument. Its `reject` takes an `Error`, as every rejection in xisto.
- When every promise of `Promise.any` rejects, it rejects with an `AggregateError` whose `errors` are the
  rejections in order. `new AggregateError(errors, message)` works, and `e instanceof AggregateError` narrows
  `e` to it. `console.log` prints one as xisto prints an `Error` (the first line of Node's stack) followed by its
  `[errors]`; Node writes the brackets of `[AggregateError: All promises were rejected]` for the one
  `Promise.any` makes, which has no stack frames.
- `Promise.try` is not there.

## BigInt

```ts
BigInt("0x1f");            // 31n (also 0b, 0o, spaces around)
BigInt(true);              // 1n
BigInt(1.5);               // RangeError, as on Node
(255n).toString(16);       // "ff": any radix from 2 to 36
BigInt.asUintN(8, 257n);   // 1n
BigInt.asIntN(8, 255n);    // -1n
```

## Typed arrays

`Uint8Array`, `Buffer`, `Float64Array`, `Float32Array` and `Int32Array` have `at(i)`. Their other recent
methods (`toSorted`, `with`, `findLast`) are not there yet.

## See also

- [Generators](./generators.md)
- [Functions and async](./functions-async.md)
- [Divergences from TypeScript](./divergences.md)
- [Known limitations](./known-limitations.md)
