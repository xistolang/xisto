---
title: Known limitations
sidebar_position: 14
---

# Known limitations

What xisto does not support yet, checked against the compiler on 12 Nov 2026. Each item says what to write
instead. Intended differences are in [Divergences from TypeScript](./divergences.md).

## Language

- **Object rest from an untyped value.** `const { a, ...rest } = { a: 1, b: 2 }` needs the source typed
  (XS6006); with an interface or class it works.
- **Generators as values.** No `.next()`, no spread, no `Array.from`: consume them with `for…of`.
- **`yield` inside `try … finally`** (XS6011).
- **`export * as ns from`** (XS1039): re-export the names one by one.
- **Side-effect imports.** `import "./x.ts"` does not run the module's top-level code. Export a function and
  call it.
- **Nested classes and enums** (XS6027): declare them at the top level.
- **String indexing** `s[i]` (XS6033): use `s.slice(i, i + 1)`.
- **`(boolean | null)[]`** (XS6039): `(int | null)[]` and `(T | null)[]` of objects and strings work.
- **Array methods.** `find` on arrays of numbers (XS7056: use `findIndex`); `splice` only as `splice(i, 1)`;
  `toSorted` and other recent methods (XS6038).
- **`Object.is`** and some other `Object` statics are missing.
- **Interfaces that mix fields and methods,** passed from a class that does not `implements` them: write
  `implements` on the class.
- **Generic arrow values** inside functions are erased: their results are copied through JSON (XS2110).
- **Prototype patching** only in the form described in [Decorators](./decorators.md).

## Runtime and library

- `Map` and `Set` `for…of` does not see entries added during the loop.
- `String.prototype.normalize` returns the string unchanged; `localeCompare` and `Date` have no ICU data:
  `Date.toString()` shows the zone abbreviation (`-03`), not its long name.
- `Map.get` of a missing key with an `int` value is a run-time error: test with `has`.
- Stack traces stop after 39 frames and do not show runtime frames.

## Tools

- `--dev` builds of programs that use `setTimeout`, `setInterval` or `clearInterval` currently fail in the C
  compiler; build without `--dev`.
- The `qbe` backend covers numbers, strings, arrays, classes, interfaces and control flow; it has no closures,
  `Map`/`Set`, streams, exceptions or networking (XS6001). The default C backend has none of these limits.
- No language server: editors get the debugger only.

## Intended semantic differences

These are not on the roadmap to change:

- `int` overflow, out-of-bounds indices and failed casts are errors, never `undefined` or a rounded value.
- One empty value, `null`.
- Floating-point sums are never reordered or fused (no FMA), so results are bit-identical to Node's; hand-
  vectorised code in other languages can be faster on such loops.
- A pattern that needs backtracking stops with a `RangeError` after a fixed number of steps, where Node would run
  on.

## See also

- [Divergences from TypeScript](./divergences.md)
- [Warnings](./warnings.md)
- [Porting a project](/docs/guides/porting)
