---
title: Modules
sidebar_position: 9
---

# Modules

xisto uses ES modules. The whole program is compiled together, and functions nobody calls are left out of the
executable, so importing a large module costs only what you use.

## Imports and exports

| Form | Supported |
|---|---|
| `import { a, b } from "./x.ts"`, `import type`, `import { type T, v }` | yes |
| `import * as ns from "./x.ts"` | yes |
| `const lib = require("./x.ts")` at the top level | yes, read as `import * as` |
| `export * from`, `export { a } from`, `export { a as b } from` (barrels) | yes |
| `export * as ns from` | no (XS1039) |
| an `import` after other declarations | yes |
| `import "./x.ts"` for its side effects | compiles, but the module's top-level code does not run |

```ts
import { twice, NAME } from "./lib/index.ts";   // a barrel: export * from "./a.ts"
import * as lib from "./lib/a.ts";
import { Box } from "@util/b";                  // tsconfig paths
```

## tsconfig paths

`compilerOptions.paths` and `baseUrl` from `tsconfig.json` are honoured, including through `extends`.

## `xisto:` and `node:`

xisto's own implementations of Node's modules are imported with the `xisto:` prefix: `xisto:fs`,
`xisto:timers/promises`, `xisto:child_process`… Importing `node:fs`, or the bare `fs`, is refused by default,
because it is not Node itself:

```text
x.ts:1:1: error[XS4001]: 'node:timers/promises' is xisto's implementation of a Node module, not Node itself
  help: import it as "xisto:timers/promises", or build with --node-compat (XISTO_NODE_COMPAT=1) to keep the Node name
```

With `--node-compat` (or `XISTO_NODE_COMPAT=1`) the Node names are accepted as written, so the same source runs on
Node and on xisto. See [Node compatibility](/docs/guides/node-compatibility).

## Packages

A bare name (`import { hi } from "greet"`) is resolved in this order:

1. relative paths;
2. `tsconfig.json` paths;
3. the directory in `XISTO_PKGS` (or `--pkgs DIR`);
4. `.xisto/pkgs/<name>` in the nearest ancestor directory; the entry is `main` in its `xisto.json`, else
   `index.ts` or `src/index.ts`.

A name that resolves nowhere is an error:

```text
error[XS5003]: cannot resolve 'nothere'
  help: import files with a relative path ("./file"), or add the package: xisto add nothere
```

Warnings inside package files are not reported. See [Projects and packages](/docs/guides/projects-and-packages).

## Circular imports

Circular imports compile. Functions that call each other across modules work as on Node. A top-level value read
by another module before its own module ran is a `ReferenceError` on Node; do not rely on the order xisto
initializes such values in.

## See also

- [Known limitations](./known-limitations.md)
- [Warnings](./warnings.md)
