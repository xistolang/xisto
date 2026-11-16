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

## Linux (x86-64 and ARM64)

From 0.4.4 every release has a Linux archive for each architecture. Unpack it anywhere and put its `bin/` on your
`PATH`:

```sh
V=0.4.4
ARCH=$(uname -m | sed -e 's/x86_64/x64/' -e 's/aarch64/arm64/')
curl -fsSLO https://github.com/xistolang/xisto/releases/download/v$V/xisto-$V-linux-$ARCH.tar.gz
tar -xzf xisto-$V-linux-$ARCH.tar.gz
export PATH="$PWD/xisto-$V/bin:$PATH"
xisto --version
```

xisto calls the system C compiler, so install clang (or gcc) first: `sudo apt-get install clang` on Debian and
Ubuntu, `sudo dnf install clang` on Fedora and Amazon Linux. The executables xisto makes need nothing installed.
The Linux archives use BearSSL for TLS and run on glibc 2.28 or newer (RHEL/Alma/Rocky 8+, Amazon
Linux 2023, Debian 10+, Ubuntu 20.04+) from 0.4.5 on; the 0.4.4 archives need glibc 2.38 (Ubuntu 24.04+, Debian 13).

## Intel Macs and other systems: build from source

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
