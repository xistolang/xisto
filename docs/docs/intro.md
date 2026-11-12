---
slug: /
title: Introduction
sidebar_label: Introduction
---

# xisto

> Software that respects the hardware it runs on.

xisto is an ahead-of-time compiler for typed TypeScript. It reads your `.ts` files, checks them under a few
stricter rules, and produces a small native executable (via C) that starts in milliseconds, uses a fraction of
the memory of a JavaScript runtime and needs nothing installed to run.

```sh
$ cat hello.ts
const xs: number[] = [1, 2, 3];
console.log(`hello ${xs.reduce((a, b) => a + b, 0)}`);

$ xisto build -o hello hello.ts && ./hello
hello 6
```

## Status

**xisto is early (0.x).** The language and the standard library are usable for real services, and the test
suite compares every program's output with Node's byte for byte, but expect gaps and breaking changes between
versions.

## What to expect

- **TypeScript as written in typed codebases.** Classes, generics, async functions, decorators, modules and the
  usual type forms compile as they are. In exchange for native speed, xisto asks for a few rules: types are real at
  run time, `null` is the only empty value, `==` never coerces, and `any` is a checked (slower) dynamic path. See
  [The language](language.md).
- **Node's behaviour.** Node's modules are available as `xisto:*` (or under their Node names with
  `--node-compat`), and the output of `console.log`, formatting and error messages follow Node's. See
  [Node compatibility](guides/node-compatibility.md).
- **npm packages as ported source packages.** Popular packages (NestJS, TypeORM, `pg`, `express`, `rxjs` and
  others) are ported with the same API in [xistolang/packages](https://github.com/xistolang/packages), each tested
  against the real package on Node. See [Projects and packages](guides/projects-and-packages.md).
- **Diagnostics that explain.** Errors carry Node-style stack traces at no cost on the happy path, and compiler
  diagnostics explain the rule and the fix.

## Where to go next

- [Install xisto](getting-started/install.md) and [write a first program](getting-started/first-program.md).
- Build [a NestJS app](getting-started/nestjs.md) in a few commands.
- Bring an existing codebase over: [Porting an existing project](guides/porting.md).
- Every command and flag: [CLI reference](reference/cli.md).
