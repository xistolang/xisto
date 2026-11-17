---
title: CLI
---

# CLI reference

`xisto` with no arguments prints this list. `xisto --version` (or `xisto version`) prints the version.

## Commands

| Command | What it does |
|---|---|
| `xisto build [--dev] [--backend=c\|qbe] [-o OUT] FILE.ts` | compile to a native executable (release, `-O3`, portable, by default; `--cpu=native` for the host's processor). Only compiles: it does not run the program. |
| `xisto run [--dev] [--backend=c\|qbe] FILE.ts [ARGS...]` | compile to a temporary file and run it, passing `ARGS` |
| `xisto emit [--backend=c\|qbe] FILE.ts` | print the generated C (or QBE IL) |
| `xisto check [--details] [--deny-warnings] [--tsc-checked] [--pkgs DIR] [PATH...]` | what a project needs to change to compile with xisto. `PATH`: a folder, a `tsconfig.json` or `.ts` files; `--details`: every place |
| `xisto test [DIR]` | build and run every `*.test.ts` under `DIR` |
| `xisto fmt [--check] FILE.ts...` | format in place, without changing tokens (`--check`: exit 1 if any file would change) |
| `xisto debug [--batch CMDS] FILE.ts [ARGS...]` | run under the debugger |
| `xisto dap` | debug adapter for IDEs (DAP over stdin/stdout) |
| `xisto clean [DIR]` | remove the build cache (`.xisto/cache`) of a project |
| `xisto init [DIR]` | start a project: `xisto.json`, `src/main.ts`, `.gitignore` |
| `xisto add NAME[@REF]...` | add packages of the registry ([github.com/xistolang/packages](https://github.com/xistolang/packages)), and their dependencies |
| `xisto add SOURCE#FOLDER[@REF]...` | the package in `FOLDER` of a git repository (URL or path) |
| `xisto add SOURCE[@REF] [--name NAME]` | a whole git repository as one package (locked by commit) |
| `xisto install` | restore every package at the commit in `xisto.lock` |

`build` and `run` without `FILE.ts` compile the `main` of the project's `xisto.json`.

### `xisto test`

A test passes when it exits with 0, leaves no live objects at exit, and, if a `NAME.expected` file exists next to
`NAME.test.ts`, prints exactly that. `assert(cond, message)` fails it. Files under `.xisto/` and `node_modules/`
are skipped.

## Flags

| Flag | Meaning |
|---|---|
| `-o OUT` | the executable's file name |
| `--dev` | fast compile, no optimization (release is the default) |
| `--debug` | a debuggable binary: `-O0 -g`, statement hooks and the debugger agent (never in release) |
| `--json` | report each error and warning as one JSON object per line on stderr (for editors and AI agents) |
| `--no-warnings` | do not report unused variables and functions |
| `--warnings` | list each rule warning (`undefined` read as null, `int` mixed with `number`...); a build prints their count |
| `--deny-warnings` | rule warnings are errors (CI); also for `xisto check` |
| `--profile` | count calls and time per function; the table goes to stderr at exit (C backend) |
| `--backend=qbe` | generate QBE IL and machine code directly, without a C compiler (a numeric subset) |
| `--target=linux-arm64\|linux-x64\|windows-x64` | an executable for another system, from any host (needs zig) |
| `--cpu=native` | code for this machine's processor (AVX2...), like Rust's `target-cpu=native` |
| `--node-compat` | accept `node:fs`, `node:sqlite`... as names of xisto's own modules (`xisto:fs`...) |
| `--tsc-checked` | the code already passes `tsc --strict`: what tsc accepted compiles, checked at run time, with a warning (`T \| null` where `T` goes: XS2107; `unknown` where `T` goes: XS2108; `number` where `int` goes: XS2109); also for `xisto check` |
| `--tls=off` | never link TLS: `fetch` URLs known only at run time must be `http://` (TLS at the load balancer) |
| `--tls=bearssl` | use BearSSL (TLS 1.2, smaller binary) instead of AWS-LC (TLS 1.3, the default) |
| `--pkgs DIR` | packages from a folder laid out like `.xisto/pkgs` (`DIR/<name>`, `DIR/@scope/<name>`) |

TLS is linked only when the program can use https: an `https://` URL, a `fetch` URL known only at run time, or
`listen(port, { cert, key })`. A program that only calls `http://` URLs carries no TLS code.

## Environment variables

| Variable | Meaning |
|---|---|
| `XISTO_REGISTRY` | another URL or path for the default package source (instead of github.com/xistolang/packages) |
| `XISTO_NODE_COMPAT=1` | same as `--node-compat` |
| `XISTO_TSC_CHECKED=1` | same as `--tsc-checked` |
| `XISTO_CPU=native` | same as `--cpu=native` |
