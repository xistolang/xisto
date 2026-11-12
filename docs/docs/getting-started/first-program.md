---
title: First program
---

# First program

Write a TypeScript file:

```ts title="hello.ts"
const xs: number[] = [1, 2, 3];
console.log(`hello ${xs.reduce((a, b) => a + b, 0)}`);
```

Build it into a native executable and run it:

```sh
xisto build -o hello hello.ts && ./hello
```

```text
hello 6
```

`xisto build` only compiles: it writes the executable (`-o hello`; without `-o`, the file's name without `.ts`)
and does not run it. The default is a release build (`-O2`).

## Run without a build step

While you are editing, `xisto run` compiles to a temporary file and runs it, passing on any arguments after the
file name:

```sh
xisto run hello.ts
```

Add `--dev` for a fast compile without optimization, for the edit loop:

```sh
xisto run --dev hello.ts
xisto build --dev hello.ts
```

Use a release build (the default) for anything you measure or deploy.

## See the C

`xisto emit` prints the C that xisto generates for a file:

```sh
xisto emit hello.ts
```

## A project instead of a file

For more than one file, or to use packages, start a project with `xisto init`: it creates `xisto.json`,
`src/main.ts` and a `.gitignore`, and then `xisto run` and `xisto build` need no file name.

```sh
xisto init hello && cd hello
xisto run                      # Hello, world!
xisto build                    # ./hello
```

Next: [a NestJS app](nestjs.md) in a few commands, or [Projects and packages](../guides/projects-and-packages.md).
