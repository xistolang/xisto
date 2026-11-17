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

Setting `xs.length` cuts an array (`xs.length = 0` empties it), as in Node, but it never makes one longer: xisto
arrays have no holes. Where the compiler knows the array's length (`const xs = [1, 2]` or
`new Array(3).fill(0)`, with no other use of `xs` before the assignment, and the new length an integer literal or a
`const` of one) a longer length is error XS6052; elsewhere it is an error at run time. Both say what to write
instead: `push(...)` to add elements, or `new Array(n).fill(value)` for an array of a given size.

```text
x.ts:2:11: error[XS6052]: setting the length of 'xs' to 5 makes it longer (its length is 3 here): xisto arrays do not grow by their length
     2 | xs.length = 5;
       |           ^
  help: add the elements with push(...), or make the array at its size: new Array(n).fill(value)
```

At run time (`xs.length = n` in a function that got `xs` as a parameter):

```text
uncaught Error: RangeError: xisto cannot make an array longer by setting its length (from 3 to 5): add the elements with push(...), or make the array at its size with new Array(n).fill(value)
```

An array literal can mix values and arrays nested to different depths; it gets TypeScript's type, and `flat()`
gives Node's result:

```ts
const rows = [1, [2, 3], 4];       // (number | number[])[], as TypeScript types it
console.log(rows.flat());          // [ 1, 2, 3, 4 ]
const tree = [[1], [2, [3]]];
console.log(tree.flat(2));         // [ 1, 2, 3 ]
```

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

## Intl and locales

`Intl` is not built into xisto: it is the `intl` package, from
[xistolang/packages](https://github.com/xistolang/packages). Add it to the project:

```sh
xisto add intl
```

With it, the program uses `Intl` as on Node, and the locale forms of the built-in methods go through it:

```ts
const brl = new Intl.NumberFormat("pt-BR", { style: "currency", currency: "BRL" });
console.log(brl.format(1234.5));                                         // R$ 1.234,50
console.log((1234.5).toLocaleString("en-US", { style: "currency", currency: "USD" })); // $1,234.50
const d = new Date(Date.UTC(2026, 10, 17, 15, 30));
console.log(d.toLocaleDateString("pt-BR", { timeZone: "UTC", dateStyle: "long" }));  // 17 de novembro de 2026
console.log(["é", "f", "e", "Z"].sort(new Intl.Collator("pt-BR").compare));         // [ 'e', 'é', 'f', 'Z' ]
```

The package has `Intl.NumberFormat`, `Intl.DateTimeFormat` and `Intl.Collator`, and takes over
`n.toLocaleString(locale, options)`, `d.toLocaleString`/`toLocaleDateString`/`toLocaleTimeString(locale, options)`
and `s.localeCompare(t, locale, options)`. Its locales are en-US and pt-BR (another `pt-*` formats as pt-BR, any
other locale as en-US); the header of its `index.ts` lists the options it supports and how it differs from Node.
Only what a program uses is compiled into it.

Without the package:

- Naming `Intl` is an error that says what to do:

  ```text
  x.ts:1:15: error[XS5005]: package 'intl' is not installed
       1 | const f = new Intl.NumberFormat("pt-BR", { style: "currency", currency: "BRL" });
         |               ^
    help: Intl is not built into xisto: install it with xisto add intl
  ```

- `d.toLocaleString()`, `toLocaleDateString()` and `toLocaleTimeString()` without arguments give en-US, as Node by
  default; with a locale or options they are error XS7014, whose help names the package.
- `n.toLocaleString(locale?, options?)` keeps a built-in form: pt-BR separators for a `pt` locale, en-US for any
  other, and the options `style: "currency"`, `currency`, `minimumFractionDigits` and `maximumFractionDigits`
  written as literals.
- `s.localeCompare(t)` (and `s.localeCompare(t, locale)`, whose locale is not read) uses ICU's root order for Latin
  text, Node's default: the letters, then the accents, then the case (lower first); other scripts come after Latin,
  by code point. The options argument needs the package.

## See also

- [Generators](./generators.md)
- [Functions and async](./functions-async.md)
- [Divergences from TypeScript](./divergences.md)
- [Known limitations](./known-limitations.md)
