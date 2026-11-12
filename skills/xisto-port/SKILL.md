---
name: xisto-port
description: Port a TypeScript/Node project (NestJS, TypeORM, Express, Mongoose...) to compile and run with the xisto native compiler, or measure how ready a project is for xisto. Use when asked to "port X to xisto", "run project X with xisto", "see what is missing to compile with xisto", or to compare a ported app with Node. Covers xisto check, packages (--pkgs), the language rules, known gaps, the order of fixes, build/run and benchmarking against Node.
---

# Porting a project to xisto

Install: copy `skills/xisto-port` from the xisto repository to `~/.claude/skills/` (all your projects) or to a
project's `.claude/skills/`.

Paths like `packages/…`, `tests/…`, `src/…` and `examples/…` are relative to your own checkout of
[xistolang/xisto](https://github.com/xistolang/xisto).

## Ground rules

- **Never edit the project's working copy.** Clone it (`git clone --local <repo> <dir>/<name>-xisto`, outside `/tmp`:
  a reboot wipes it) and work on a branch such as `xisto/<app>`. The owner reviews the diff and decides what goes back.
  Code from a private project never goes into the xisto repository: tests there use synthetic code.
- **Order of fixes:** 1) the compiler (a rule that makes natural code work for everyone), 2) a package in `packages/`
  (the npm API, same output as Node), 3) only then the project's code, in small commits that say why. The less a team
  must change in its app, the easier adoption is: every idiomatic TS form an app uses is a compiler task; rewriting
  the app is the last resort, with the reason written down.
- **Output must match Node bit for bit.** Every package and language change is tested against the same program on
  Node (`tests/ok` compares stdout+stderr with Node's). Every `.expected` comes from running the program on Node (or
  bun when Node cannot run the form); never write one by hand.
- Compiler and CLI messages are in English.

## 1. Measure: `xisto check`

```
xisto check [--pkgs DIR] [--tests] <project | app dir | tsconfig.json | main.ts>   # summary by kind of change
xisto check --details [--pkgs DIR] <path>                                          # every file:line:col with code and help
```
- Run it **from a scratch directory**, never inside the project (it must not write there); confirm with
  `git -C <project> status --porcelain` before and after.
- Reads `tsconfig.json`/`tsconfig.base.json` (`paths`, `baseUrl`, `extends`), resolves folder imports to `index.ts`
  and `./x.js` to `./x.ts`, follows barrels.
- Tests (`*.spec.ts`, `__tests__`, `__mocks__`) are counted apart; `--tests` includes them.
- `xisto check <main.ts>` gives the **build closure** (only what main.ts reaches): use it when the goal is "compile".
  Diagnostics are shown only under the entry's folder, but the build also compiles the shared libs it reaches (monorepo
  `libs/`). To see every error in every reached module: `XISTO_PKGS=<pkgs> xisto __check1 <main.ts>` prints one
  `module\t…` line per module, then one JSON line per error (severity, code, file, line, message), and
  near the end a `quiet\tN` line (follow-on errors hidden).
- `--pkgs DIR`: packages outside the project (a folder laid out like `.xisto/pkgs`: `DIR/<name>`, `DIR/@scope/<name>`);
  see Scripts. `--tsc-checked` (or `XISTO_TSC_CHECKED=1`): for code that already passes `tsc --strict`, what tsc
  accepted compiles with run-time checks and warnings XS2107–XS2109.
- Aggregate `--details` / `__check1` output with a small script (group by code + message with quoted names blanked)
  and attack the biggest group; recount after each fix. One cause often hides hundreds of lines (a single
  `createParamDecorator` callback, one rule helper). Never read the whole output: a monorepo gives 200k+ lines.

Kinds of change and what usually fixes them:
| kind | typical cause | fix |
|---|---|---|
| module or package not available | npm package / `node:` module not ported | port the package (`packages/`) or the module (runtime) |
| Node module import | `node:x` / bare `fs`, `path`… | automatic: `xisto:x` (or `--node-compat`) |
| syntax xisto does not read | language gap (see §3) | compiler |
| missing type annotation | field/param/return without type | annotate, or compiler inference |
| dynamic value | `any`, `unknown` misuse, `undefined` | `null` + `T \| null`, typed values |
| type error under xisto's rules | int vs number, union picks, coercion | adjust types; `==` with mixed types is refused on purpose |
| dynamic path (any) | `any` in the code | not a change: listed apart, compiles with warning XS2007 |

**Trust a count only when it is complete:**
- `__check1` must print its `quiet` line (a missing one means it died midway), exit 0, and print as many `module` lines as the previous full count.
  A count that suddenly drops is usually a crash, a parse error (XS1xxx stops the type check) or a class-level error
  that stops the whole check, not progress. A fast count is a false count: the time should be in proportion
  (seconds for ~1 000 modules, not 0.4 s).
- If `xisto check` says "the compiler stopped early", the count is partial. Build xisto with ASan
  (`cc -std=c11 -g -O0 -D_GNU_SOURCE -fsanitize=address -fsanitize-recover=address -o x-asan src/*.c build/rt_embed.o -lm`,
  run with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=0`) and recount with it after big changes.
- Warnings are deduplicated per place but each generic instance counts in its own context: compare totals between
  rounds, not absolute numbers.
- A diagnostic whose line/column point to the wrong place comes from a generic instance's body: look for the generic
  that line calls.

## 2. Packages

- Live in `packages/<name>` (`packages/@scope/<name>`): `xisto.json` (`name`, `version`, `implements:
  {"<npm name>": "^x"}`, `main`, optional `native: ["native/x.c"]`, `needs`, `dependencies`), `index.ts`, optional
  `native/*.c`. The header comment of each `index.ts` says what it implements and what it does not.
- A project finds packages in `.xisto/pkgs/` (in an ancestor dir) or in `--pkgs DIR` for `build`/`check`.
  `xisto add <git url | path>[@ref] [--name NAME]` adds one locked by commit (`xisto.lock`); `xisto install` fetches them.
- Ported (check each header for the exact subset):
  - Nest: `@nestjs/{common,core,config,swagger,schedule,platform-express,typeorm,passport,throttler,jwt,mongoose}`.
    common/core include guards (APP_GUARD, `@UseGuards`), interceptors (APP_INTERCEPTOR, `@UseInterceptors`, Nest
    order), Reflector, SetMetadata, `ctx.getHandler()/getClass()`. `@nestjs/config`: `registerAs`, `forRoot({ load,
    envFilePath })`, `get('a.b.c')` (env first, as Nest). `@nestjs/jwt`: HMAC only (HS256/384/512).
  - Data: `typeorm` (subset: DataSource.query, entities/views, Repository, QueryBuilder, QueryRunner; postgres and
    oracle dialects), `pg`, `oracledb` (thin; see below), `mongodb` (v6 driver subset; no TLS, no `mongodb+srv://`),
    `mongoose` (v8 over that driver), `redis`, `ioredis`, `@elastic/elasticsearch` (v8 subset).
  - Web/auth: `express`, `helmet`, `compression` (accepted, answers uncompressed), `class-validator`,
    `class-transformer`, `jsonwebtoken`, `jose` (RS256 only), `passport-jwt` (HS256 + RS256), `jwks-rsa`, `argon2`.
  - Other: `rxjs` (one-value Observable: `pipe` with map/tap/catchError/finalize/mergeMap, of/from/throwError/
    lastValueFrom), `nestjs-pino`, `@elastic/ecs-pino-format`, `exceljs` (streaming XLSX, constant memory), `slug`,
    `reflect-metadata`, `dd-trace` (API kept, tracing off, one stderr line).
- `oracledb`: thin TNS/TTC, O5LOGON 12c and also accounts with only the old **10g verifier** (it logs in and warns
  once per user; node-oracledb thin refuses those with NJS-116, thick mode accepts them). Positional binds via
  `execute`, named binds via `executeNamed(sql, record)` (a parameter that is "array or object" is not supported).
  PL/SQL OUT/IN OUT binds with `outBinds`; CLOB/BLOB OUT come whole (text / lowercase hex), not as `Lob` objects.
  Errors are `OracleError` (`errorNum`, `code`, `offset`), copied by TypeORM's `QueryFailedError`.
  `initOracleClient()` throws (thin only). Not yet: RETURNING INTO, REF CURSOR, `Lob` objects.
- Nest reflection: a decorated class's `ClassInfo` has `isInstance(v)` and `base` (the decorated class it extends);
  a base class a framework must see needs a decorator (`@Injectable()`). Mixins: `class X extends F(args)` lowers to
  `extends F` + `F.__mixin(args)`; write a `static __mixin(...): (c: ClassInfo) => void` on F. Async methods called
  through `MethodInfo.invoke` return the settled value.
- **New package:** list what the project imports from it (names + call counts), implement that subset with the npm
  semantics, and test against the real npm package on Node (a reference app outside the repo; delete its
  `node_modules` after). Use synthetic copies of the app's own call sites as tests: they find language bugs too.
- **Every package must say what it accepts without implementing.** A silently ignored option compiles fine and is
  the costliest gap (see §5).
- Databases for tests: local Docker containers on 127.0.0.1 only, passwords in a 0600 file, containers removed at the
  end. Oracle: `gvenzl/oracle-free:23-slim-faststart` (needs ~2 GB of Docker memory).

## 3. Language rules and known gaps

Rules (by design):
- **No `undefined` value; `null` is the only empty.** `?.`, `??`, `??=` work on `T | null`; optional `x?: T` has an
  internal "absent" state (omitted in JSON, `undefined` in console.log, as Node). Writing `undefined` is an error: use
  `null` / `x == null`. `T | undefined` in types reads as `T | null`.
- `==`/`!=` mean `===`/`!==` when both sides have the same type; `x == null` is null or absent; coercing comparisons
  are refused.
- `int` (±2^53−1, overflow is an error) vs `number`; int mixed with number is number. `object` holds JSON.
- **`any` is a checked dynamic path** (warning XS2007, with its cost: ~7 ns per dynamic field read vs ~0.6 ns typed).
  An object of a class seen as `any` keeps its struct; `x.f`, `x[k]`, `x.f = v`, `k in x` go through the class's
  field table. A field the class does not declare is a TypeError (JS would read `undefined`). An `any` where a `T`
  goes is checked at run time. Arithmetic, `===`, truthiness, `??`, `?.`, templates, JSON and console.log work on
  `any`; **calling a method on an `any` is refused (XS2017)**: give it its type.
- **Promises:** `await f()` and a bare `f();` are direct calls (a bare `f();` runs to the end before the next line;
  Node runs it to its first await). A promise exists only where it is kept: `const p = f()`, `Promise<T>`
  fields/params/arrays, `.then/.catch/.finally`, `new Promise`, `Promise.resolve/reject`. `XISTO_SPAWN_LOG=1 xisto
  build …` prints every call kept as a promise. Not yet: `Promise.allSettled`.
- Structural copies are copies: a class object passed where a data shape goes is copied field by field, so writing
  through the parameter does not change the caller's object. An interface with methods is nominal.
- Rule warnings (XS2101–XS2106: `undefined` as null, never-null comparison, int mixed with number, uninferred T, int
  as a condition, `s + x`) compile; `xisto build` prints their count, `--warnings` lists each, `--deny-warnings`
  makes them errors.

Supported forms (all like Node; see `tests/ok/`):
- Types: inline object types; `Record<K, V>` (index, `in`, `delete`, `for…in`, `Object.keys/values/entries`,
  `Object.fromEntries`); `A & B`, `Omit/Pick/Partial/Required/NonNullable/Readonly/ReadonlyArray/Awaited`,
  `ReturnType<typeof f>`, `T['field']`, `T[number]`, `typeof x`, `keyof T`, conditional aliases (true branch);
  literal types (`'a' | 'b'`, `1 | 2`); `x is T` predicates; `satisfies`; `as const`; `symbol` / `unique symbol`.
- Inference: return types (from the `return`s; recursion needs a written type), field types from the value,
  object literals without a declared type (an anonymous interface per shape), `new Set(xs)`/`new Map(entries)`
  without type args, a generic's T from the target (`const rows: Row[] = await ds.query(sql)`).
- Classes: `class X extends Error` with fields and `instanceof` in `catch`; `new Error(m, { cause })`;
  `extends PartialType/OmitType/PickType/IntersectionType(Y)` (fields copied; class-validator rules of Y are NOT
  inherited yet); interfaces with methods (an abstract base of the class that `implements` them); `x!: T`;
  `let x: T;` assigned later.
- Modules: `import type`, `import { type X, y }`, re-exports, barrels, `export *`; `import * as ns`; top-level
  `const ns = require('m')`; an `import` after declarations.
- Statements and expressions: flow narrowing as TypeScript (`a?.f === 'x'`, assignments, `instanceof`/`typeof`,
  `Array.isArray`); IIFE and `f(a)(b)`; labels; `do…while`; `String.raw`; rest params and spreads in calls;
  nested destructuring and swaps; `void expr`; `x.m?.()`; module-level generic arrows; defaults in params.
- Node modules: `node:fs`, `path`, `crypto` (createHash, createHmac, randomUUID), `os`, `async_hooks`
  (AsyncLocalStorage), `stream` (synchronous, no backpressure wait), and others as `xisto:*`.

Known gaps (rewrite in the clone meanwhile, or better, fix the compiler):
- `import 'm';` for side effects is skipped (its top-level code does not run); `require()` inside a function or `try`;
  `export * as ns` and renamed re-exports (XS1038/XS1039); two imports with the same name from different packages.
- Object rest `const { a, ...rest } = o`; generic arrows inside functions (hoist to the module); type-level indexing
  (`Parameters<typeof f>[0][number]`, `T[keyof T]`); mapped types; `DeepPartial<T>`; generic interfaces with methods
  (XS8113: write concrete interfaces); return type `: this`; overloads.
- `{ ...x }` of a Record or an `as any` into a typed target, and spreads of a call: copy entries by name.
- `Partial<T>` has no run-time presence (absent = null): to iterate over "the keys given" use `Record<string, unknown>`.
- Plain-number strictness: `if (n)` on a plain `number` (compare with 0), `x.padStart(n)` with `n: number`.
- A literal cannot implement an interface method: use a class, or function-typed fields.
- Union narrowing works on identifiers only: put `t[0].ok` in a const first.
- A cast of an Error to a shape (`err as { errorNum?: number }`) is refused: use `instanceof`.
- `type X = ImportedClass` is a distinct type: use the class name.
- A `const` read by a closure in its own initializer needs its type written.
- An inline object type inside a generic method is one class per instance: name it when entries are compared with `===`.
- `query()` of typeorm returns `any` (as TypeORM): `unknown as Row[]` checks every element and throws; give the type
  to the query (`query<Row[]>(…)` or a typed target) so the rows are decoded.

## 4. Build and run

```
xisto build --pkgs DIR -o <out> <entry.ts>     # main.ts of the app; tsconfig paths are honoured; release by default
xisto build --dev --node-compat ...             # fast compile; node:* names accepted (XISTO_NODE_COMPAT=1)
xisto run --dev <entry.ts>                      # fast edit loop
```
- The build's checker stops at the first type error of a file: iterate with `__check1` for the full list, or a small
  script that builds and prints the first error.
- Config: the app reads `.env` like on Node (`@nestjs/config`). Point database settings at a local or dev database
  the owner provides; never invent credentials.
- **Use release for anything measured or hit with real endpoints.** A `--dev` build has big stack frames in generated
  functions and a request handler runs on a 64 KB coroutine stack, so a deep endpoint can crash (SIGBUS in
  `___chkstk_darwin` on macOS) in `--dev` only. Native package code must not put big arrays on the stack.
- Never run two app builds at once (memory), nor a load test while a build runs.

### 4a. Against a real environment

- **Environment from a container/orchestrator:** load the exported env with a small runner (parse the file and
  `execve`, not `source`: JSON values with quotes), and drop what belongs to the platform, not the app: APM/profiler
  injectors (`LD_PRELOAD`), `LD_LIBRARY_PATH`, vendor APM variables, `KUBERNETES_*`, `*_SERVICE_*`/`*_PORT_*`, `HOME`,
  `PATH`, `HOSTNAME`. Override `PORT`. Keep the file 0600; never print a secret (mask keys with
  PASS/SECRET/KEY/TOKEN/PWD).
- **Check the app's write gates (dry-run flags, queues) before starting**, and send only read-only requests to a
  shared database.
- **Reach first:** `nc -z <host> <port>` for the database and any JWKS URL. Measure TCP connect time and compare it
  with the request time to count round trips before calling something slow (extra rollbacks or separate commits
  show up as extra round trips).
- **Health endpoints swallow the error** (`catch { database = 'down' }`). Don't rebuild the app to see it: write a
  15-line program with the same package (connect + `SELECT 1`) and run it with the same env.
- **One login attempt at a time against a real database.** Every wrong attempt counts as a failed login and can lock
  a real person's account: one attempt, then think. Prefer your own test account over a shared/service one. (Oracle:
  a login that hangs ~60 s and drops with ORA-03113, no ORA-01017, is a malformed message: the password was never
  judged.)
- **Bugs hide behind the first failure:** each fix often uncovers the next (login → protocol → stack → round trips).
  Go one request at a time and re-measure after each.

### 4b. Comparing with Node (same app, same database)

- **Build Node from the ORIGINAL code**, not the clone (the clone may use xisto APIs such as `executeNamed` and no
  longer pass `tsc`): `git worktree add --detach ../<app>-node $(git merge-base <main branch> xisto/<app>)`, with one
  `npm ci --ignore-scripts` from the same lock.
- **Same Node version as production**: download the official tarball, check its SHA256, don't touch the system Node.
- **Run it like the production image:** follow the Dockerfile (alias symlinks into `dist`, image env such as
  `UV_THREADPOOL_SIZE`).
- **A database both can log into** (e.g. a local container with a 12c-verifier user when Node thin cannot log into
  the real account).
- **Equalise the work before measuring:** turn off APM tracing on Node when there is no agent (some tracers keep one
  span object per request); remove per-request timers both sides would treat differently (rate limiters that keep a
  `setTimeout` per request), as a local, reverted patch; check both write the same log lines per request, and also
  measure Node with a quieter log level.
- **Measure:** time to first 200, idle RSS, then a load generator (e.g. `oha`, 60 s × 32 connections per endpoint):
  status codes, req/s, p50/p99, CPU per 1k requests, peak RSS, memory after. **Check the status codes**: tens of
  thousands of req/s of 400s once looked like a result. Delete multi-GB logs after.
- **Suspected leak? Prove it:** run Node with `--max-old-space-size=256 --heapsnapshot-near-heap-limit=1` and
  aggregate the snapshot by constructor; one object type with ~1 instance per request names the culprit.
- Write down the method, the table, the caveats and the raw numbers next to the result.

## 5. Pitfalls learned

- **Silent run-time gaps are the costliest.** A decorator that compiles but does nothing (e.g. a query DTO never
  filled nor validated) does not show in any error count. Before calling an app "ported", run its main routes and
  compare with Node; an end-to-end example on Node with the real framework (`examples/nest-forms`) catches these.
- **A variance message (XS8018) does not prove it is variance.** Read the two types in the message: often it is a
  compiler gap with the same message (a defaulted param not optional in the function type, a `new Map(xs.map(...))`
  inferred as `unknown`, an inferred anonymous return vs an interface with the same fields). If the app's code is
  natural TS, the fix is in the compiler.
- A big group of "object literal needs a declared type" (XS3006) can be a missing package API, not a language form
  (e.g. `@Controller({ path, version })`). Open three examples before deciding where to fix.
- Before rewriting a "one-off" in the clone, look for the TS form underneath: most unique-looking errors are a
  handful of compiler gaps.
- **"Starts without the database" depends on each dependency's design.** Read the config first: an optional module
  (registered only when its URI is set, `@Optional()` models) only has to compile; a fail-loud one (throws without
  its URI, as on Node) needs a local container and a ported driver.
- A test failure can be the test (pg `query` returns `QueryResult`, not rows): check against the Node package first.
- A cache of promises of different types (`Map<string, Promise<unknown>>` + `as Promise<T>`) works, but the
  converted promise is another object (`===` is false) and settles one microtask later.
- Compiler contributors:
  - A crash only in -O2 is memory: build once with ASan (§1). A header change (`src/xisto.h`) needs a full rebuild.
  - A C macro that repeats its argument (`__typeof__(x)` + `x`) explodes on nested chains; `xisto emit` +
    `clang -fsyntax-only` under a memory watch bisects a heavy C file to the line.
  - Don't rebuild `build/xisto`, edit `packages/` or edit `tests/run.sh` while the suite runs (it reads all of them
    as it goes, and the tests compile packages through symlinks). Run suites one at a time: two at once fight over
    the http test's port. Compile a side binary into `build/` (e.g. `cc -std=c11 -O1 -D_GNU_SOURCE -w -o
    build/xisto-dev src/*.c build/rt_embed.o -lm`) to count while the suite runs.
  - A merge resolved "keeping both sides" is unverified until the full suite runs on the result; two branches
    touching the same runtime helper can conflict silently.
  - Each git worktree with a build is ~0.5–1 GB: remove it once its branch is merged.

## Scripts

Packages dir for `--pkgs` (symlinks, nothing copied), run from the xisto checkout:
```
R=<scratch>/pkgs; mkdir -p $R/@nestjs $R/@elastic
for p in packages/*; do n=$(basename $p); case $n in @*|xisto) continue;; esac; ln -s $PWD/$p $R/$n; done
for s in @nestjs @elastic; do for p in packages/$s/*; do ln -s $PWD/$p $R/$s/$(basename $p); done; done
```
After a new package lands, re-run the loops, or the check keeps reporting it as missing.

Group `__check1` errors by cause:
```
XISTO_PKGS=<scratch>/pkgs xisto __check1 <main.ts> > out.txt 2>&1; echo "exit $?"
grep -c "^module" out.txt; grep "^quiet" out.txt   # full module count, and a quiet line must be there
python3 - out.txt <<'PY'
import sys, json, re, collections
c = collections.Counter()
for l in open(sys.argv[1]):
    if l.startswith('{'):
        e = json.loads(l); c[(e.get('code'), re.sub(r"'[^']*'", "'_'", e.get('message', '')))] += 1
for (code, msg), n in c.most_common(30): print(n, code, msg)
PY
```
