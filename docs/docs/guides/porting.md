---
title: Porting an existing project
---

# Porting an existing project

An existing TypeScript/Node project (NestJS, TypeORM, Express...) usually needs some changes before it compiles
with xisto. Start by measuring how many, and of which kind.

The repository also has [skills/xisto-port](https://github.com/xistolang/xisto/blob/main/skills/xisto-port/SKILL.md),
a longer guide to the method (also usable as a Claude Code skill).

## Ground rules

- **Work on a copy.** Clone the project and work on a branch, so the owner reviews the diff and decides what goes
  back.
- **Fix in this order:**
  1. the **compiler**: a rule that makes natural code work for everyone;
  2. a **package** in [xistolang/packages](https://github.com/xistolang/packages): the npm API, with the same output
     as Node;
  3. only then the **project's code**, in small commits that say why.

  The less a team must change in its app, the easier adoption is: every idiomatic TypeScript form an app uses is a
  compiler task, and rewriting the app is the last resort.
- **Output must match Node.** Compare the ported app's behaviour with the same app on Node.

## 1. Measure with `xisto check`

```sh
xisto check <project | app dir | tsconfig.json | main.ts>      # a summary by kind of change
xisto check --details <path>                                   # every file:line:col, with code and help
```

- Run it from a scratch directory, not inside the project.
- It reads `tsconfig.json` / `tsconfig.base.json` (`paths`, `baseUrl`, `extends`), resolves folder imports to
  `index.ts` and `./x.js` to `./x.ts`, and follows barrels.
- Tests (`*.spec.ts`, `__tests__`, `__mocks__`) are counted apart; `--tests` includes them.
- `xisto check <main.ts>` gives the build closure: only what `main.ts` reaches. Use it when the goal is to compile
  the app.
- `--pkgs DIR` uses packages from a folder outside the project, such as a checkout of xistolang/packages:

  ```sh
  git clone https://github.com/xistolang/packages <scratch>/packages
  xisto check --pkgs <scratch>/packages <project>
  ```

- `--tsc-checked` (or `XISTO_TSC_CHECKED=1`): for code that already passes `tsc --strict`, what tsc accepted
  compiles with run-time checks and warnings (XS2107–XS2109).

With `--details`, group the output by code and message and attack the biggest group first; recount after each
fix. One cause often hides hundreds of lines.

## 2. Kinds of change

| Kind | Typical cause | Fix |
|---|---|---|
| module or package not available | npm package or `node:` module not ported | port the package (xistolang/packages) or the module |
| Node module import | `node:x` / bare `fs`, `path`... | automatic: `xisto:x`, or `--node-compat` |
| syntax xisto does not read | a language gap (see [The language](../language/index.md)) | compiler |
| missing type annotation | a field, parameter or return without a type | annotate, or compiler inference |
| dynamic value | `any`, `unknown` misuse, `undefined` | `null` and `T \| null`, typed values |
| type error under xisto's rules | `int` vs `number`, union picks, coercion | adjust the types; `==` with mixed types is refused on purpose |
| dynamic path (`any`) | `any` in the code | not a change: listed apart, compiles with warning XS2007 |

## 3. Build and run

```sh
xisto build --pkgs DIR -o <out> <entry.ts>      # the app's main.ts; tsconfig paths are honoured; release by default
xisto build --dev --node-compat ...              # fast compile; node:* names accepted
xisto run --dev <entry.ts>                       # fast edit loop
```

- Use a release build (the default) for anything you measure or hit with real requests: a `--dev` build has big
  stack frames, and a deep request handler can run out of its stack in `--dev` only.
- The app reads `.env` as on Node (`@nestjs/config`). Point database settings at a local or development database.

## 4. Compare with Node

Before calling an app ported, run its main routes on both and compare. The costliest gaps are silent: a decorator
or an option that compiles but does nothing does not show in any error count. Each package says, in the header of
its `index.ts`, what it accepts without implementing.
