---
slug: /
title: Introduction
sidebar_label: Introduction
sidebar_position: 1
---

# Xisto

**Xisto is a strict subset of TypeScript, compiled ahead of time to small native executables.** You write
TypeScript under a few stricter rules; `xisto build` checks it and produces one executable that starts at once,
uses little memory and needs nothing installed to run. The same source still runs on Node.

```sh
$ cat hello.ts
const xs: number[] = [1, 2, 3];
console.log(`hello ${xs.reduce((a, b) => a + b, 0)}`);

$ xisto build -o hello hello.ts && ./hello
hello 6
```

## Three numbers

Measured on an Apple M4, November 2026, against Node v26.12.0. Method, raw files and the cases where Xisto loses
are in [Benchmarks](benchmarks/index.md).

| What | Xisto | Node |
|---|---|---|
| Peak memory of a small program (`fib`) | 1.3 MB | 66.1 MB |
| HTTP hello, `/ping` (oha, 64 connections) | 231,097 req/s | 136,913 req/s |
| Startup of a real NestJS app (Conduit) | 21 ms | 268 ms |

## What to expect

- **It is early (0.x).** The compiler and the standard library run real services, and the test suite compares
  every program's output with Node's byte for byte, but expect gaps and breaking changes between versions.
- **There are rules.** Types are real at run time, `null` is the only empty value, `==` never coerces, and `any`
  is a checked, slower dynamic path. Each rule exists so the types can become native code. See
  [The language](language/index.md).
- **The differences are written down.** Every place where Xisto behaves differently from TypeScript on Node is in
  the [divergence table](language/divergences.md). Read it before you port anything.
- **npm packages come as ported source packages.** NestJS, TypeORM, `pg`, `express`, `rxjs` and others are ported
  with the same API in [xistolang/packages](https://github.com/xistolang/packages).

## Map of the docs

- **[Why Xisto](why.md)**: the manifesto, what we measure and how we describe the project.
- **Getting started**: [install](getting-started/install.md), [a first program](getting-started/first-program.md),
  [a NestJS app](getting-started/nestjs.md), [projects and packages](guides/projects-and-packages.md).
- **[The language](language/index.md)**: types, `null`, equality, `any`, classes, async, modules, errors,
  decorators, and the warnings the compiler gives.
- **[Performance](performance/model.md)** and **[Benchmarks](benchmarks/index.md)**: the cost model, how to write
  fast code, how we measure, and every result with its machine and date.
- **[Node compatibility](guides/node-compatibility.md)**: Node's modules as `xisto:*`, and what differs.
- **[Porting](guides/porting.md)**: bringing an existing TypeScript project over, step by step.
- **Reference**: the [CLI](reference/cli.md) and the [project files](reference/project-files.md).
