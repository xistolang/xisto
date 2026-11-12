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

**Status: early (0.x).** The language and the standard library are usable for real services, and the test suite
compares every program's output with Node's byte for byte, but expect gaps and breaking changes between versions.

## Install

macOS on Apple silicon:

```sh
brew install xistolang/tap/xisto
```

xisto calls the system C compiler (`clang` from the Xcode Command Line Tools: `xcode-select --install`).
Linux and Intel Macs: build from source (below).

## Use

```sh
xisto build -o app main.ts      # release build (-O2)
xisto build --dev main.ts       # fast compile for the edit loop
xisto run main.ts [args...]     # compile to a temporary file and run
xisto check <dir|tsconfig.json> # what a project needs to compile with xisto, by kind of change
xisto test [dir]                # runs *.test.ts (asserts, zero leaked objects, .expected output)
xisto fmt [--check] file.ts     # formats without changing tokens
xisto emit file.ts              # prints the generated C
xisto init [dir]                # a new project (xisto.json, src/main.ts, .gitignore)
xisto add <npm-name>...         # packages from xistolang/packages, locked by commit; `xisto install` restores
xisto --version
```

Useful flags: `--json` (diagnostics as JSON for editors and agents), `--profile` (time per function at exit),
`--backend=qbe` (no C compiler: QBE → assembler, a numeric subset), `--target=linux-arm64|linux-x64|windows-x64`
(cross-compile with zig).

## The language

xisto compiles TypeScript as written in typed codebases. What it asks in exchange for native speed:

- **Types are real.** `int` and `number` are distinct at run time (`int` overflow is an error, not a silent float);
  objects have a fixed layout; generics are compiled per use.
- **`null` is the only empty value.** There is no `undefined` value: `x?: T`, `T | undefined`, `?.`, `??` all work
  on `T | null`, and an absent optional field is still omitted from JSON and printed as `undefined`, like Node.
- **`==` never coerces.** It means `===` when both sides have the same type; comparisons that would coerce are refused.
- **`any` is a checked dynamic path.** It works (field access by name, calls, JSON), each value is checked when it
  meets a typed slot, and the compiler warns where it is used, because it is roughly 10× slower than the typed path.
- **Memory** is reference-counted by the compiler; the test suite requires zero live objects at exit.

Errors carry Node-style stack traces at no cost on the happy path, and diagnostics explain the rule and the fix.

## Node compatibility

Node's modules are available as `xisto:*` (`import { readFile } from 'xisto:fs'`), or under their Node names with
`--node-compat`: `fs`, `path`, `http`/`https` (server and `fetch`, TLS via AWS-LC or BearSSL), `crypto`, `stream`,
`async_hooks`, `os`, `url`, `sqlite`, timers, and more. Output, formatting of `console.log` and error messages follow
Node's.

npm packages are ported as source packages with the same API, in
[xistolang/packages](https://github.com/xistolang/packages), each tested against the real package on Node: NestJS
(`@nestjs/common`, `core`, `config`, `swagger`, `schedule`, `passport`, `jwt`, `throttler`, `typeorm`, `mongoose`,
`platform-express`), TypeORM (Postgres and Oracle), `oracledb` (thin protocol), `pg`, `mongodb`/`mongoose`, `redis`,
`ioredis`, `express`, `class-validator`, `class-transformer`, `rxjs`, `jsonwebtoken`, `jose`, `passport-jwt`,
`jwks-rsa`, `exceljs`, `@elastic/elasticsearch`, `nestjs-pino`, `helmet`, `argon2` and others.

## A project: NestJS in a few commands

```sh
brew install xistolang/tap/xisto
xisto init hello-nest && cd hello-nest     # xisto.json, src/main.ts, .gitignore
xisto run                                  # Hello, world!
xisto add @nestjs/common @nestjs/core @nestjs/platform-express
```

`xisto add` fetches the packages from [xistolang/packages](https://github.com/xistolang/packages) and their
dependencies with them (here `express`, `rxjs`, `class-validator` and `class-transformer`). Then write the app in
`src/main.ts`, as on Node:

```ts
import { Controller, Get, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';

@Controller()
class AppController {
  @Get()
  hello(): string {
    return 'Hello from xisto';
  }
}

@Module({ controllers: [AppController] })
class AppModule {}

async function bootstrap(): Promise<void> {
  const app = await NestFactory.create(AppModule);
  await app.listen(3123);
}
bootstrap();
```

```sh
xisto build                      # ./hello-nest, a native executable of about 1 MB
./hello-nest &
curl localhost:3123              # Hello from xisto

rm -rf .xisto && xisto install   # a fresh checkout: restores the packages from xisto.lock
xisto build
```

`xisto add` copies each package into `.xisto/pkgs/` and pins it in `xisto.lock` (source, folder, commit);
`xisto install` restores exactly that, like `npm ci`. Other sources: `xisto add <git-url>#<folder>[@ref]` or a whole
repository with `xisto add <git-url>[@ref]`. A build can also point to a folder of packages with `--pkgs DIR`.

Porting an existing project: start with `xisto check`, then see [skills/xisto-port](skills/xisto-port/SKILL.md),
a guide (also usable as a Claude Code skill) with the method, the language rules and the known gaps.

## Build from source

Needs a C compiler (clang or gcc), `make`, `python3`, `xxd` and `cmake` (for AWS-LC, downloaded once and checked by
SHA-256; `make TLS=bearssl` skips it and uses BearSSL, in the tree).

```sh
git clone https://github.com/xistolang/xisto && cd xisto
make                 # build/xisto
make test            # the suite: tests/ok compares stdout+stderr with Node (needs node), tests/err the diagnostics
make dist VERSION=x.y.z   # a release archive in dist/
```

Runs on macOS (kqueue) and Linux (epoll), ARM64 and x86-64. `sh tests/linux/test.sh linux/arm64` runs the suite in
Docker.

## Layout

```
src/          the compiler: lexer → parser → checker → C emitter (and the QBE emitter)
runtime/      the runtime embedded in every generated program (memory, strings, event loop, net, TLS, regex)
packages/     the xisto:* standard packages (npm ports live in xistolang/packages)
tests/        ok/ (output compared with Node), err/ (expected diagnostics)
bench/        benchmark programs, scripts and versioned results (bench/results/)
examples/     a larger program (a REST API); NestJS examples live in xistolang/packages
editors/      VS Code extension
third_party/  BearSSL, QBE, SQLite (and AWS-LC, downloaded)
```

## License

[Apache-2.0 WITH LLVM-exception](LICENSE). The exception means programs compiled with xisto, which carry its runtime,
do not need to reproduce xisto's license notice.
