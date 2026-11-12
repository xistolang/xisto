---
title: Projects and packages
---

# Projects and packages

## Start a project

```sh
xisto init hello      # or `xisto init` in the current folder
```

`xisto init [DIR]` creates three files (an existing one is left as it is):

- `xisto.json`: the project's `name`, its `main` (`src/main.ts`) and its `dependencies` (empty).
- `src/main.ts`: a hello world, the program's entry.
- `.gitignore`: ignores `.xisto/` (the build cache and the installed packages) and the executable `xisto build`
  makes.

Inside a project, `xisto build` and `xisto run` without a file compile the `main` of the nearest `xisto.json` (the
current folder or one above it). `xisto build` names the executable after the project's `name`.

## Add packages

npm packages are ported to xisto as source packages with the same API, in the monorepo
[xistolang/packages](https://github.com/xistolang/packages). Add them by their npm name:

```sh
xisto add @nestjs/common @nestjs/core @nestjs/platform-express
```

`xisto add`:

- fetches each package from the registry and **its dependencies with it** (a package's own `xisto.json` lists
  them; they come from the same repository at the same commit);
- copies each package into `.xisto/pkgs/<name>` (`.xisto/pkgs/@scope/name` for scoped names);
- records what you asked for in `xisto.json` (`"dependencies"`) and pins every package, direct and indirect, by
  commit in `xisto.lock`;
- never touches your code, and never runs anything from a package: a package is only source code.

A specific revision of the registry: `xisto add NAME@REF`. Indirect packages that nothing needs
any more are removed. A dependency already locked at another commit stays there, with a warning that says which
`xisto add` moves it.

Repositories are cloned once into a shared cache (`~/.cache/xisto/git`, or `$XDG_CACHE_HOME/xisto/git`); once
installed, a project does not depend on that cache.

## Restore on a fresh checkout

`.xisto/` is not committed. `xisto install` restores every package at the exact commit in `xisto.lock`, like
`npm ci`:

```sh
xisto install
```

A dependency written into `xisto.json` by hand (or a project without a lock) is resolved by `xisto install` and
then locked.

## Other sources

Besides the registry, a package can come from any git repository, given as a URL or a local path:

```sh
xisto add <git-url>#<folder>[@ref]            # the package in one folder of a repository
xisto add <git-url>[@ref] [--name NAME]       # a whole repository as one package
```

`@ref` is a git revision, such as a branch or a tag; the lock pins the commit it resolved to. `--name` names the
package (one package at a time).

## Another registry

`XISTO_REGISTRY` points the default source to another URL or a local checkout, for example while you develop a
package:

```sh
XISTO_REGISTRY=~/src/packages xisto add my-package
```

## A folder of packages: `--pkgs`

`xisto build`, `xisto run` and `xisto check` also accept `--pkgs DIR`: packages from a folder laid out like
`.xisto/pkgs` (`DIR/<name>`, `DIR/@scope/<name>`). A checkout of xistolang/packages is one as it is:

```sh
git clone https://github.com/xistolang/packages ~/src/packages
xisto build --pkgs ~/src/packages -o app src/main.ts
```

## What a package contains

A package is a folder with a `xisto.json` (`name`, `version`, `implements: {"<npm name>": "^x"}`, `main`,
optionally `native` C files, `needs` and `dependencies`), an `index.ts`, and optionally `native/*.c`. The header
comment of each `index.ts` says what it implements of the npm package and what it does not: check it for the exact
subset.

The file formats are in [xisto.json and xisto.lock](../reference/project-files.md).
