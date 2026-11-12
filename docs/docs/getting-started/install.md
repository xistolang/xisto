---
title: Install
---

# Install

## macOS on Apple silicon

```sh
brew install xistolang/tap/xisto
```

xisto calls the system C compiler: `clang` from the Xcode Command Line Tools. If you do not have them yet:

```sh
xcode-select --install
```

Check the installation:

```sh
xisto --version
```

## Linux and Intel Macs: build from source

Building needs a C compiler (clang or gcc), `make`, `python3`, `xxd` and `cmake` (for AWS-LC, the default TLS
library, downloaded once and checked by SHA-256; `make TLS=bearssl` skips it and uses BearSSL, which is in the tree).

```sh
git clone https://github.com/xistolang/xisto && cd xisto
make                 # build/xisto
```

The compiler is `build/xisto`; put it (or a link to it) on your `PATH`. xisto runs on macOS and Linux, on ARM64
and x86-64. At build time, xisto needs a C compiler on the machine (clang or gcc); the executables it makes need
nothing installed.

## Cross-compiling

`xisto build --target=linux-arm64|linux-x64|windows-x64` makes an executable for another system from any host.
It needs [zig](https://ziglang.org) installed. See the [CLI reference](../reference/cli.md).

## Editor support

The repository has a VS Code extension in
[`editors/`](https://github.com/xistolang/xisto/tree/main/editors). Editors and AI agents can also read
diagnostics as JSON with `--json`.

Next: [your first program](first-program.md).
