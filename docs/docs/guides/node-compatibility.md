---
title: Node compatibility
---

# Node compatibility

## Node's modules

Node's modules are available under the `xisto:` prefix:

```ts
import { readFile } from 'xisto:fs';
```

With `--node-compat` (or `XISTO_NODE_COMPAT=1`), the same modules are also accepted under their Node names
(`fs`, `node:fs`, `node:sqlite`...), so code written for Node imports them unchanged:

```sh
xisto build --node-compat -o app main.ts
```

Available modules include:

- `fs`, `path`, `os`, `url`
- `http` / `https`: a server and `fetch`, with TLS (AWS-LC by default, or BearSSL)
- `events` (`EventEmitter`), `process`, `timers` (see below)
- `crypto` (`createHash`, `createHmac`, `randomUUID`)
- `stream` (synchronous, no backpressure wait)
- `async_hooks` (`AsyncLocalStorage`)
- `sqlite`
- timers, and more

The output of `console.log`, value formatting and error messages follow Node's, and errors carry Node-style stack
traces.

## events

`import EventEmitter, { once } from "node:events"` (or `{ EventEmitter }`, or `"events"`) gives Node's
`EventEmitter`. A class can extend it:

```ts
import { EventEmitter } from "node:events";

class Job extends EventEmitter {
  run(n: number): void {
    for (let i = 1; i <= n; i++) this.emit("progress", i, n);
    this.emit("done");
  }
}
const job = new Job();
const onProgress = (i: number, n: number): void => console.log(`${i}/${n}`);
job.on("progress", onProgress);
job.once("done", () => console.log("done"));
job.run(2);
job.off("progress", onProgress); // the same function given to on()
```

Supported: `on`/`addListener`, `once`, `prependListener`, `prependOnceListener`, `off`/`removeListener`,
`removeAllListeners`, `emit`, `listenerCount` (with a listener too), `listeners`, `rawListeners`, `eventNames`,
`setMaxListeners`/`getMaxListeners` (with Node's warning on stderr), `EventEmitter.defaultMaxListeners`, the
`'newListener'` and `'removeListener'` events, an `'error'` with no listener thrown, `errorMonitor`, and the
functions `once(emitter, name)` (a promise of the arguments, rejected by an `'error'`), `getEventListeners`,
`setMaxListeners`, `getMaxListeners`, `listenerCount`.

A listener with its own parameter types, `(i: number, n: number) => void`, is accepted where Node's typings say
`(...args: any[]) => void`. The compiler wraps it so that each argument is checked as the listener takes it, and
the wrapper keeps the function itself: `off(fn)`, `listeners()` and `===` see the same function, as in Node.

Differences from Node:

- Event names are strings. `errorMonitor` is a reserved string, not a symbol.
- A listener called with fewer arguments than it declares gets `null` for the missing ones (Node: `undefined`).
  If the parameter's type does not admit `null`, that is a `TypeError`.
- `on()` and the other chaining methods return `EventEmitter`. xisto has no `this` return type, so a subclass
  chain `new Job().on(...)` is typed as `EventEmitter`.
- `rawListeners()` gives the listeners themselves, without the once wrappers.
- Not available yet: `events.on(emitter, name)` (async iteration), `captureRejections`, `EventTarget` helpers.

## timers

The globals `setTimeout`, `setInterval`, `setImmediate`, `clearTimeout`, `clearInterval`, `clearImmediate` and
`queueMicrotask` work as in Node, also imported from `node:timers`. The delay is optional and extra arguments go
to the callback: `setTimeout(greet, 10, "ana")`. A timer is its numeric id (`NodeJS.Timeout` is `int`), and the
Timeout methods work on it: `setTimeout(f, 1000).unref()`, `ref()`, `hasRef()`, `refresh()`, `close()`. An
unref'd timer does not keep the program alive.

The order matches Node's event loop:

- `process.nextTick` callbacks run first, then microtasks (promise reactions, `queueMicrotask`).
- `setImmediate` callbacks run once both are empty.
- Timers that expire together each run with their own microtasks drained before the next one.
- In the program's top-level code the microtasks come before the `nextTick` callbacks. That is what Node does
  for a `.ts` file, which it runs as an ES module.

Differences from Node: `typeof setTimeout(...)` is `"number"`, not `"object"`. Extra arguments are read when the
callback runs, not when the timer is set.

## process

Beyond `process.argv`, `process.env`, `process.pid`, `process.cwd()`, `process.exit(code)`,
`process.exitCode` and `process.stdout`/`stderr.write`, these are available with no import, or through
`import process from "node:process"`:

| Member | Notes |
|---|---|
| `platform`, `arch`, `version`, `versions`, `release`, `execPath`, `execArgv`, `argv0`, `title`, `ppid` | `version` is the Node version whose API xisto follows (`v26.0.0`); `versions.xisto` is set |
| `hrtime()`, `hrtime(prev)`, `hrtime.bigint()`, `uptime()` | monotonic clock |
| `memoryUsage()`, `memoryUsage.rss()`, `cpuUsage(prev?)`, `resourceUsage()` | `heapUsed` is an estimate from the objects alive (xisto has no heap like V8's) |
| `nextTick(fn, ...args)` | before promise reactions, as in Node |
| `on`/`once`/`off`/`emit`/`listenerCount`... with `'exit'`, `'beforeExit'`, `'warning'` | `'exit'` runs on `process.exit()` and at the normal end; `'beforeExit'` may start more work |
| `on('SIGINT' \| 'SIGTERM' \| 'SIGHUP' \| 'SIGUSR1' \| 'SIGUSR2' ...)` | a listener replaces the default action; it also takes precedence over the HTTP server's graceful shutdown |
| `kill(pid, signal)`, `chdir(dir)`, `umask()`, `getuid()`/`getgid()`/`geteuid()`/`getegid()`, `emitWarning()`, `abort()` | errors carry Node's messages (`ENOENT: no such file or directory, chdir ...`) |

Differences from Node: `'uncaughtException'` and `'unhandledRejection'` listeners are not called (an uncaught
error ends the program with exit code 1, as when no listener is set). `process.stdin` is not available yet; read standard
input with `xisto:io`.

## TLS

TLS is linked only when the program can use https: an `https://` URL, a `fetch` URL known only at run time, or
`listen(port, { cert, key })`. A program that only calls `http://` URLs carries no TLS code. The build flags
`--tls=off` and `--tls=bearssl` choose otherwise (see the [CLI reference](../reference/cli.md)).

## npm packages

npm packages are ported as source packages with the same API, in
[xistolang/packages](https://github.com/xistolang/packages), each tested against the real package on Node. Among
them:

- NestJS: `@nestjs/common`, `core`, `config`, `swagger`, `schedule`, `passport`, `jwt`, `throttler`, `typeorm`,
  `mongoose`, `platform-express`
- Data: TypeORM (Postgres and Oracle), `oracledb` (thin protocol), `pg`, `mongodb` / `mongoose`, `redis`,
  `ioredis`, `@elastic/elasticsearch`
- Web and auth: `express`, `helmet`, `class-validator`, `class-transformer`, `jsonwebtoken`, `jose`,
  `passport-jwt`, `jwks-rsa`, `argon2`
- Other: `rxjs`, `exceljs`, `nestjs-pino`, and others

Many ports implement a subset of the npm API. The header comment of each package's
`index.ts` says what it implements and what it does not. Add them with `xisto add` (see
[Projects and packages](projects-and-packages.md)).
