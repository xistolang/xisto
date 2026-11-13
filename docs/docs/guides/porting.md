---
title: Porting an existing project
---

# Porting an existing project

An existing TypeScript/Node project (NestJS, TypeORM, Express...) usually needs some changes before it compiles
with xisto. Start by measuring how many, and of which kind.

You can follow the steps on this page yourself, or hand them to an AI coding agent with the port skill below.

## Porting with an AI agent: the `xisto-port` skill

[`skills/xisto-port/SKILL.md`](https://github.com/xistolang/xisto/blob/main/skills/xisto-port/SKILL.md) is the
method on this page written for an agent, plus what the ports so far have taught: the ported packages and their
limits, the language rules and known gaps, how to read a `xisto check` count, how to compare with Node, and the
pitfalls. It is a [Claude Code skill](https://code.claude.com/docs/en/skills), and plain Markdown that
any agent can read.

**Install it** for all your projects, or only for one:

```sh
mkdir -p ~/.claude/skills/xisto-port
curl -fsSL https://raw.githubusercontent.com/xistolang/xisto/main/skills/xisto-port/SKILL.md \
  -o ~/.claude/skills/xisto-port/SKILL.md
```

For one project, use `.claude/skills/xisto-port/SKILL.md` inside it instead. Run the same command again to update.

**Use it.** In Claude Code, call it by name with the goal, or just ask in your own words; the skill's description
makes the agent load it when the task is a port:

```text
/xisto-port port apps/api to xisto: it must compile and answer /health like it does on Node
```

```text
what is missing for this project to compile with xisto?
```

With another agent, point it at the file: "read skills/xisto-port/SKILL.md and follow it to port this project".

**What the agent does with it:**

1. Clones the project and works on a branch (`xisto/<app>`); it never edits your working copy, so you review the
   diff and decide what goes back.
2. Runs `xisto check` from a scratch directory, groups the errors by cause and attacks the biggest group first,
   recounting after each fix. It distrusts a count that drops too fast: that is usually a crash, not progress.
3. Fixes in the order below: the compiler, then a package, and only then your code, in small commits that say why.
4. Builds a release binary, runs it against a local or development database you provide, and compares the main
   routes with the same app on Node, original code, same Node version.

**What it is told never to do:** invent credentials, print a secret (it masks keys named like PASS, SECRET, KEY,
TOKEN), try more than one login at a time against a real database (a wrong attempt can lock a real person's
account), run a load test while a build runs, or put code from your project into the xisto repository.

You stay in charge of the decisions: what to change in the app, which database to use, and what goes back to your
repository.

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
