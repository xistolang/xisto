---
title: Contributing
---

# Contributing

xisto is developed at [github.com/xistolang/xisto](https://github.com/xistolang/xisto); ported npm packages live
in [github.com/xistolang/packages](https://github.com/xistolang/packages). Issues and pull requests are welcome.

## Build from source

Needs a C compiler (clang or gcc), `make`, `python3`, `xxd` and `cmake` (for AWS-LC, downloaded once and checked by
SHA-256; `make TLS=bearssl` skips it and uses BearSSL, in the tree).

```sh
git clone https://github.com/xistolang/xisto && cd xisto
make                 # build/xisto
make test            # the suite: tests/ok compares stdout+stderr with Node (needs node), tests/err the diagnostics
make dist VERSION=x.y.z   # a release archive in dist/
```

xisto runs on macOS (kqueue) and Linux (epoll), ARM64 and x86-64. `sh tests/linux/test.sh linux/arm64` runs the
suite in Docker.

## Tests compare with Node

- `tests/ok/` holds programs whose stdout and stderr are compared with Node's, byte for byte. Every `.expected`
  output comes from running the program on Node; it is never written by hand.
- `tests/err/` holds programs whose diagnostics are checked.
- Every language or package change comes with a test that runs the same program on Node.
- Compiler and CLI messages are in English.

## Layout

```text
src/          the compiler: lexer → parser → checker → C emitter (and the QBE emitter)
runtime/      the runtime embedded in every generated program (memory, strings, event loop, net, TLS, regex)
packages/     the xisto:* standard packages (npm ports live in xistolang/packages)
tests/        ok/ (output compared with Node), err/ (expected diagnostics)
bench/        benchmark programs, scripts and versioned results (bench/results/)
examples/     a larger program (a REST API); NestJS examples live in xistolang/packages
editors/      VS Code extension
third_party/  BearSSL, QBE, SQLite (and AWS-LC, downloaded)
```

## Packages

A port of an npm package goes to [xistolang/packages](https://github.com/xistolang/packages), not to this
repository:

- Implement the subset of the npm API that is needed, with the npm package's semantics, and test it against the
  real npm package on Node: the same program must give the same output.
- Every package says, in the header comment of its `index.ts`, what it implements and what it accepts without
  implementing. A silently ignored option compiles fine and is the costliest gap.
- Tests use synthetic code: code from a private project never goes into either repository.

## Documentation

This site is in [`docs/`](https://github.com/xistolang/xisto/tree/main/docs) (Docusaurus). To preview it:

```sh
cd docs
npm ci
npm start
```

## License

xisto is licensed under [Apache-2.0 WITH LLVM-exception](https://github.com/xistolang/xisto/blob/main/LICENSE). The
exception means programs compiled with xisto, which carry its runtime, do not need to reproduce xisto's license
notice.
