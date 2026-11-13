---
title: xisto.json and xisto.lock
---

# xisto.json and xisto.lock

A project has two files at its root. Both are written by `xisto init` and `xisto add`; you can edit `xisto.json`
by hand.

## xisto.json

What the project is and what you asked for:

```json
{
  "name": "hello-nest",
  "main": "src/main.ts",
  "dependencies": {
    "@nestjs/common": "*",
    "@nestjs/core": "*",
    "@nestjs/platform-express": "*"
  }
}
```

| Field | Meaning |
|---|---|
| `name` | the project's name; `xisto build` names the executable after it |
| `main` | the entry that `xisto build` and `xisto run` compile when no file is given |
| `dependencies` | the packages added directly, name → spec |

A dependency's spec is one of:

- `"*"`: the registry's default branch;
- a revision of the registry, such as `"v1.2"`;
- `"SOURCE[#FOLDER][@REF]"`: a git repository (URL or path), optionally one folder of it, optionally at a revision.

A dependency written here by hand is resolved and locked by the next `xisto install`.

## xisto.lock

Every package, direct and indirect, pinned by commit; one line per package, so it diffs cleanly:

```json
{
  "@nestjs/common": {"source": "https://github.com/xistolang/packages", "subdir": "@nestjs/common", "commit": "<sha>"},
  "rxjs": {"source": "https://github.com/xistolang/packages", "subdir": "rxjs", "commit": "<sha>", "indirect": true}
}
```

| Field | Meaning |
|---|---|
| `source` | the git repository (URL or path) |
| `subdir` | the package's folder in that repository (only when set) |
| `ref` | the revision asked for (only when set) |
| `commit` | the exact commit installed |
| `indirect` | `true` for a package that is there because another package needs it (only when set) |

Commit `xisto.lock` with the project. `xisto install` restores exactly what it lists into `.xisto/pkgs/`, which
is not committed.

## A package's xisto.json

A package (in [xistolang/packages](https://github.com/xistolang/packages) or any repository) has its own
`xisto.json`: `name`, `version`, `implements` (`{"<npm name>": "^x"}`), `main`, and optionally `native` (C files
such as `["native/x.c"]`), `needs` and `dependencies` (name → version).
