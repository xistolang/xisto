---
title: Warnings
sidebar_position: 13
---

# Warnings

Some rules compile with a warning instead of an error: the code works, but it does something implicit or slow
that you should know about. These are the rule warnings, each with a stable code.

## The rule warnings

| Code | Category | Means | Fix |
|---|---|---|---|
| XS2007 | dynamic-path | `any` is used: access by name, checked at run time, about 10× slower | declare a type ([any](./any.md)) |
| XS2101 | deprecated | `undefined` is read as `null` | write `null` |
| XS2102 | deprecated | comparing a value that is never null with `null`: always the same result | remove the test, or declare `T \| null` |
| XS2103 | implicit | `int` mixed with `number`: the result is `number` | `Number(i)` or `Math.trunc(x)` |
| XS2104 | implicit | a type parameter could not be inferred: it is `unknown` | pass it: `empty<Type>(...)` |
| XS2105 | implicit | an `int` as a condition, read as `!== 0` | compare explicitly |
| XS2106 | implicit | `+` turns a non-string into text | a template, or `String(x)` |
| XS2107 | implicit | `--tsc-checked`: a value that may be null where a non-null goes | `if (x !== null)`, or `x!` |
| XS2108 | implicit | `--tsc-checked`: an `unknown` where a type goes | type it where it is made, or `x as T` |
| XS2109 | implicit | `--tsc-checked`: a `number` where an `int` goes, truncated | declare it `int`, or `Math.trunc` |
| XS2110 | implicit | the result of a generic function value is copied through JSON | a concrete return type, or a generic function |

Each comes with its place and a `help:` line:

```text
w1.ts:4:22: warning[XS2106]: '+' turns int into text, as JavaScript does
     4 | const label = "n=" + n;
       |                      ^
  help: write it as a template (`a ${x}`) or convert explicitly: String(x)
```

## In a build

`xisto build` and `xisto run` print one line with the count per code, not each warning:

```text
xisto: 2 warnings (XS2106 2): --warnings lists them, xisto check --details shows where
```

| Flag | Effect |
|---|---|
| `--warnings` | list every warning with its place |
| `--no-warnings` | print nothing about them |
| `--deny-warnings` | warnings are errors (use it in CI) |
| `--json` | one JSON object per diagnostic: `severity`, `code`, `category`, `file`, `line`, `col`, `message`, `help`, `source` |

Warnings inside package files (`.xisto/pkgs/`) are not reported. Each place is reported once.

`xisto check` lists them apart from the changes a project needs, and `--deny-warnings` counts them as changes:

```text
  warnings (compile; to review)                   uses  files
    XS2101   deprecated                                  1      1
    XS2103   implicit                                    1      1
```

Unused locals and unused functions get plain warnings without a code.

## `--tsc-checked`

`--tsc-checked` is an opt-in mode for migration, never the default. It accepts what `tsc --strict` already proved
and xisto would refuse, and puts a run-time check where xisto cannot prove it:

| Code | Without the flag | With `--tsc-checked` |
|---|---|---|
| XS2107 | error: `string \| null` where `string` goes | compiles; `TypeError: the value is null` if it is |
| XS2108 | error: `unknown` where `string` goes | compiles; checked at run time |
| XS2109 | error: `number` where `int` goes | compiles; truncated (`7 / 2` stored as `3`) |
| XS2105 | error XS8022 on a `number` condition | read as `!== 0 and not NaN` |

Use it to get a large codebase running, then remove the warnings one by one and drop the flag.

## See also

- [any](./any.md)
- [Numbers](./types.md)
- [null and absent values](./null.md)
- [CLI reference](/docs/reference/cli)
