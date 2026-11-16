---
title: Install
---

# Install

## Homebrew: macOS on Apple silicon and Linux

```sh
brew install xistolang/tap/xisto
xisto --version
```

The same command works on macOS on Apple silicon and on Linux x86-64 and ARM64 with
[Homebrew on Linux](https://docs.brew.sh/Homebrew-on-Linux) (glibc 2.28 or newer: RHEL/Alma/Rocky 8+, Amazon
Linux 2023, Debian 10+, Ubuntu 20.04+).

xisto calls the system C compiler when it builds a program; the executables it makes need nothing installed.

- macOS: `clang` from the Xcode Command Line Tools. If you do not have them yet: `xcode-select --install`.
- Linux: the compiler Homebrew on Linux already asks for (`build-essential` on Debian and Ubuntu, `gcc` or
  "Development Tools" on Fedora, RHEL and Amazon Linux). xisto uses `clang` when it is on your `PATH` (it makes
  faster programs), else `cc`; `XISTO_CC` picks another one. To add clang: `sudo apt-get install clang` or
  `sudo dnf install clang`.

If you do not have Homebrew on Linux yet:

```sh
sudo apt-get install build-essential procps curl file git    # Debian, Ubuntu (dnf on Fedora/RHEL/Amazon Linux)
NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"
brew install xistolang/tap/xisto
```

## Linux without Homebrew: the release archive

Every release has a Linux archive for each architecture. Unpack it anywhere and put its `bin/` on your `PATH`:

```sh
V=0.4.5
ARCH=$(uname -m | sed -e 's/x86_64/x64/' -e 's/aarch64/arm64/')
curl -fsSLO https://github.com/xistolang/xisto/releases/download/v$V/xisto-$V-linux-$ARCH.tar.gz
tar -xzf xisto-$V-linux-$ARCH.tar.gz
export PATH="$PWD/xisto-$V/bin:$PATH"
xisto --version
```

Install a C compiler first, as above. The Linux archives use BearSSL for TLS and run on glibc 2.28 or newer from
0.4.5 on; the 0.4.4 archives need glibc 2.38 (Ubuntu 24.04+, Debian 13).

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
