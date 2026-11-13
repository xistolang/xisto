---
title: Equality and truthiness
sidebar_position: 4
---

# Equality and truthiness

JavaScript converts values silently in `==` and in conditions. xisto keeps the forms that do not convert and refuses
or flags the others.

## `==` and `!=`

`==` and `!=` are accepted and mean `===` and `!==` whenever no conversion could happen: both sides of the same
type (`int` and `number` count as one), or one side `null`. `x == null` is true for `null` and for an absent field.

A comparison that would convert one side is refused:

```text
x.ts:4:15: error[XS8148]: '==' between int and string would convert one of them
     4 | console.log(a == s);
       |               ^
  help: JavaScript would turn the string into a number first; say which: Number(s) === n, or String(n) === s
```

Why: `==` appears thousands of times in real codebases, almost always between values of one type. Refusing it
everywhere would cost a port many edits for nothing; accepting the converting cases would hide bugs.

Objects compare by identity, as in JavaScript.

## Conditions

| Value in `if`, `while`, `!`, `&&`, `||` | Meaning |
|---|---|
| `boolean` | itself |
| `T \| null` (objects, strings, arrays, nullable primitives) | not `null`; for `int \| null`, `0` is also false |
| `string` | not empty |
| object, array | always true |
| `int` | `!== 0`, with warning XS2105 |
| `number` | refused (XS8022) |

```text
x.ts:6:5: warning[XS2105]: an int as a condition: read as '!== 0'
  help: compare explicitly: x !== 0 (or x > 0)
```

```text
x.ts:2:5: error[XS8022]: condition must be boolean, but this is number
  help: there is no truthiness for numbers in xisto; compare explicitly, e.g. 'x !== 0'
```

A `number` condition hides the `NaN` case, so it is refused. With `--tsc-checked` it compiles, reads as
`!== 0 and not NaN`, and warns with XS2105.

## `&&` and `||`

`a || b` with two values of the same type gives that type: `s || "default"` is a `string`.

`a && b` gives a `boolean` when its operands are not booleans. On Node it gives one of the operands:

| Expression | xisto | Node |
|---|---|---|
| `m && m.length` (`m = "abc"`) | `true` | `3` |
| `s && "x"` (`s = ""`) | `false` | `""` |

Inside a condition the result is the same. When you need the value, write `m !== null ? m.length : 0`.

## See also

- [null and absent values](./null.md)
- [Numbers](./types.md)
- [Warnings](./warnings.md)
