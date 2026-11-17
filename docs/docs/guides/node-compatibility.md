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

- `fs`, `fs/promises`, `path`, `os`, `url` and `import.meta` (see [fs, path, os, url](#fs-path-os-url))
- `http`: Node's `createServer`/`request`/`get` (below), and xisto's own `Server` with `fetch`, with TLS for
  `https://` (AWS-LC by default, or BearSSL)
- `events` (`EventEmitter`), `process`, `timers`, `http`, `net`, `dns`, `child_process` (see below)
- `readline`, `readline/promises` and `tty`, with `process.stdin` (see [readline](#readline) and [tty](#tty))
- `util`, `querystring`, `string_decoder` (below)
- `http` / `https`: a server and `fetch`, with TLS (AWS-LC by default, or BearSSL)
- `crypto`: hashes, HMAC, AES ciphers, pbkdf2/scrypt/hkdf, keys and RSA/ECDSA signatures (see [crypto](#crypto))
- `zlib`: deflate, gzip, raw deflate and Brotli (see [zlib](#zlib))
- `stream`, `stream/promises`, `stream/web`, and `fs.createReadStream`/`createWriteStream` (see [stream](#stream))
- `async_hooks` (`AsyncLocalStorage`)
- `buffer` (`Buffer`, also a global; see [Buffer](#buffer))
- `sqlite`
- `assert` and `assert/strict` (see [assert](#assert)), `test` (see [test](#test)), `perf_hooks` (see
  [performance](#performance))
- timers, and more

The globals `globalThis`, `crypto`, `console` and `performance` are described in [globalThis and the global
crypto](#globalthis-and-the-global-crypto), [console](#console) and [performance](#performance).

The output of `console.log`, value formatting and error messages follow Node's, and errors carry Node-style stack
traces.

## stream

`node:stream` is a port of Node's own stream state machines (`lib/internal/streams`, Node 26): the same flags and
the same `process.nextTick` deferrals, so the events of a stream come in Node's order and on Node's ticks.

```ts
import { Readable, Transform, Writable, pipeline } from "node:stream";
import type { TransformCallback } from "node:stream";

class Upper extends Transform {
  override _transform(chunk: Buffer, encoding: string, callback: TransformCallback): void {
    callback(null, chunk.toString().toUpperCase());
  }
}
const out: string[] = [];
const sink = new Writable({
  write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
    out.push(chunk.toString());
    cb();
  },
});
pipeline(Readable.from(["a", "b"]), new Upper(), sink, (err) => console.log(err ? err.message : "done", out));
```

Supported:

| | |
|---|---|
| `Readable` | paused and flowing modes, `push`/`unshift`, `_read`, `read(n)`, `'data'`, `'readable'`, `'end'`, `'close'`, `'error'`, `'pause'`/`'resume'`, `pipe`/`unpipe` with backpressure (`'drain'`), `pause`/`resume`/`isPaused`, `setEncoding` (a character split between chunks is kept whole), `objectMode`, `highWaterMark`, `destroy(err)`, `Readable.from(array \| generator \| async generator)`, `for await (const chunk of readable)`, the `readable*` properties |
| `Writable` | `write` (false above the `highWaterMark`), `'drain'`, `end(chunk?, encoding?, cb?)`, `'finish'`/`'prefinish'`, `_write`/`_writev`/`_final`, `cork`/`uncork`, `setDefaultEncoding`, `decodeStrings`, `objectMode`, the `writable*` properties |
| `Duplex`, `Transform`, `PassThrough` | `allowHalfOpen`, `readable`/`writable: false`, `readableObjectMode`/`writableObjectMode`, `_transform` (pushing, or `callback(null, data)`), `_flush` |
| all | `_construct`, `_destroy`, `destroy(err, cb)`, `autoDestroy`, `emitClose`, `destroyed`/`closed`/`errored` |
| functions | `pipeline(...streams, cb)`, `finished(stream, [options], cb)`, `destroy(stream, err?)`, `isDestroyed`, `isReadable`, `isWritable`, `isErrored`, `isDisturbed`, `getDefaultHighWaterMark`/`setDefaultHighWaterMark` (64 KiB, 16 objects) |
| `Stream` | the module itself, as in Node: `Stream.Readable`, `Stream.Transform`, `Stream.pipeline`... (also `stream.Stream.X`), in `new`, `extends`, `instanceof` and calls; a plain `Stream`'s `pipe` is Node's legacy one, a `Writable`'s emits `ERR_STREAM_CANNOT_PIPE` |
| `node:stream/promises` | `pipeline(...streams)`, `finished(stream, options?)` |
| `node:stream/consumers` | `text`, `json`, `buffer`, `bytes`, `arrayBuffer` (a `Uint8Array`: xisto has no `ArrayBuffer`) of a `Readable`; no `blob` |
| `node:fs` | `createReadStream(path, { start, end, highWaterMark, encoding, fd, flags, autoClose })` (by name or as `fs.createReadStream`) and `createWriteStream(path, { flags, encoding, start, fd })`, `ReadStream`/`WriteStream` (`'open'`, `'ready'`, `bytesRead`/`bytesWritten`, `close()`); errors as Node's (`ENOENT: no such file or directory, open '...'`, `code`, `syscall`) |

Errors carry Node's `code` and message (`ERR_STREAM_PUSH_AFTER_EOF`, `ERR_STREAM_WRITE_AFTER_END`,
`ERR_STREAM_DESTROYED`, `ERR_STREAM_PREMATURE_CLOSE`, `ERR_MULTIPLE_CALLBACK`, `ERR_INVALID_ARG_TYPE`...).

Chunks: in byte mode a chunk is a `Buffer` (a string pushed or written is converted, a `Uint8Array` is taken as
one), or a string after `setEncoding`. In object mode a chunk is any value. A listener and a method override may
name the type they expect: `on("data", (chunk: Buffer) => ...)`, `_transform(chunk: Buffer, ...)` over the base's
`chunk: any`, `_read(size: int)` over the base's `number`, `_write(..., callback: TransformCallback)` over the base's
`(error?: Error | null) => void`; the value is checked when it arrives. `chunk.toString()`
and `chunk.toString("hex")` work on an `any` chunk.

Option functions (`read`, `write`, `writev`, `final`, `destroy`, `construct`, `transform`, `flush`) are written as
properties. An arrow function has no `this`: refer to the stream by its variable (give the variable its type,
`const w: Writable = new Writable({ ... })`, when the function reads a property of it), or write
`read: function (this: Readable, size: number) { this.push(x); }`. Method shorthand (`read() { ... }`) in an object
literal is not supported; a subclass that overrides `_read` and the others works as in Node.

Event timing: `process.nextTick` callbacks run after the current callback and before its microtasks, and the
program's top-level code is an ES module's (its microtasks first), as in Node. `fs` streams do their reads,
writes, `open` and `close` on a later turn of the loop (`setImmediate`), as Node's thread pool delivers them.

Differences from Node:

- `Readable` is a subclass of `Writable` in xisto, so that `Duplex` can be both (xisto classes have one base): a
  `Duplex`, `Transform` or `PassThrough` goes wherever a `Readable` or a `Writable` goes, as in Node. The price:
  `x instanceof Writable` is also true for a plain `Readable` (`instanceof Readable` is exact; test
  `x.writable`), and a `Readable` has `write()`/`end()`, which throw. `Writable` has no `pipe()`.
- `Readable.from(generator)` runs the generator as a task: a value is produced when the previous one has been
  read, one step later than Node, which reads one ahead. For an array it reads as Node does.
- Breaking out of `for await (const c of readable)` does not destroy the stream (xisto's generators have no
  `return()`); the loop ending normally or with an error does as in Node.
- `pipeline` takes streams only: no generator functions or web streams among them, no `{ signal }`. There is no
  `compose`, `addAbortSignal`, `Readable.fromWeb`/`toWeb`, `wrap`, or the operators (`map`, `filter`,
  `toArray`...).
- A missing argument is `null` (`finished` calls back with `null`, not `undefined`); errors are `Error` objects
  (Node's `AggregateError` of two errors is the later one).

### stream/web

`node:stream/web` has `ReadableStream` (with `getReader()`/`read()`/`releaseLock()`, `cancel`, `pipeTo`,
`pipeThrough`, `tee`, `for await`), `WritableStream` (`getWriter()` with `write`/`close`/`abort`/`ready`/
`closed`/`desiredSize`), `TransformStream`, `TextEncoderStream`, `TextDecoderStream`,
`CountQueuingStrategy` and `ByteLengthQueuingStrategy`. They are globals too, as in Node: a program that makes a
`new ReadableStream(...)` (or names `WritableStream`, `TransformStream`...) gets them without an import, and
`ReadableStream<string>` takes its type arguments (the chunks are read as `any`). A source, sink or
transformer is an object of functions written as properties (`start: (c) => { ... }`), each returning nothing or a
promise.

Differences from Node: no byte streams (`type: "bytes"`, BYOB readers), no `ReadableStream.from`, no transfer;
the steps of one stream come in Node's order, but the number of microtasks between two of them can differ (a
`pull` may run after a read that Node lets wait for it). `fetch()`'s `response.body` is still null (fetch reads
the body whole).

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
functions `once(emitter, name)` (a promise of the arguments, rejected by an `'error'`), `on(emitter, name, { close })`
(the emits' arguments as an async iterator: `for await (const [a, b] of on(emitter, "data"))`), `getEventListeners`,
`setMaxListeners`, `getMaxListeners`, `listenerCount`, also as statics of `EventEmitter`.

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
- `events.on()`: leaving the `for await` loop with `break` does not remove its listeners; end it with the `close`
  option when the emitter lives on. No `signal` option.
- Not available yet: `captureRejections`, `EventTarget` helpers.

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

| `on('uncaughtException' \| 'uncaughtExceptionMonitor')`, `setUncaughtExceptionCaptureCallback(fn)`, `hasUncaughtExceptionCaptureCallback()` | the listener gets `(err, origin)`; the work the error interrupted ends and the event loop goes on (an interval keeps firing). A listener that throws ends the program with code 7 |
| `on('unhandledRejection')` | `(reason, promise)` once the microtasks have run; with no such listener the rejection goes to `'uncaughtException'` with the origin `"unhandledRejection"` (Node's default `--unhandled-rejections=throw`) |
| `stdin`, `stdout`, `stderr` as streams | `process.stdin` is a `Readable` over fd 0 (see [tty](#tty)); `process.stdout`/`stderr` are `Writable`s (`isTTY`, `columns`, `rows`, a `pipe()` target); `process.stdout.write(x)` itself stays the built-in fast path |

With no `'uncaughtException'` listener an uncaught error is printed (`uncaught Error: message` and its stack) and the
program ends with code 1, after the `'exit'` listeners, as in Node.

Differences from Node:

- the promise an `'unhandledRejection'` listener gets is typed `unknown` (`(reason: Error, promise: unknown) => ...`):
  a `Promise<void>` parameter cannot be checked at run time;
- an error thrown by the top level reports the origin `"unhandledRejection"` (Node runs a `.ts` file with an import or
  an export as an ES module, whose top level is a promise; a CommonJS file reports `"uncaughtException"`), and only the
  `'uncaughtException'` listeners get it, as in Node 26;
- an `async` function called without `await` whose body throws before its first real wait (one that parks) throws
  synchronously, so it reaches `'uncaughtException'`, not `'unhandledRejection'`;
- `'rejectionHandled'` is not emitted, and `'warning'` only by `process.emitWarning`;
- a program that listens for `'uncaughtException'` (or `'exit'`) keeps its top-level variables as globals (main has
  a landing for the errors a listener takes); other programs compile as before.

## readline

`node:readline` and `node:readline/promises` (also `readline.promises`) read lines from any `Readable`:
`process.stdin`, `fs.createReadStream`, a stream of your own.

| API | Notes |
|---|---|
| `createInterface({ input, output, prompt, crlfDelay, terminal })`, `createInterface(input, output)` | `'line'` for each line, `'close'` at the end of the input (or `rl.close()`), `'pause'`, `'resume'` |
| `rl.question(query, cb)`; `readline/promises`: `await rl.question(query)` | the query goes to `output`; the next line is the answer. A pending promise is rejected (`AbortError`) when the interface closes |
| `rl.prompt()`, `setPrompt()`, `getPrompt()`, `write(data)`, `pause()`, `resume()`, `close()`, `closed` | |
| `for await (const line of rl)` | the input pauses while 1024 lines wait to be read |
| `clearLine`, `clearScreenDown`, `cursorTo`, `moveCursor`; `readline/promises`' `Readline` (`commit()`) | ANSI sequences written to the stream |

Lines end at `\n`, `\r\n` or a lone `\r` (a `\r\n` split across two chunks is one ending when the `\n` comes within
`crlfDelay` ms); the last line is emitted at the end of the input even without a newline. As in Node, with piped
input every line already read is emitted at once: answers of `await rl.question()` asked one after another can be
lost (the callback form, which asks the next question from the callback, gets each line).

Differences from Node: there is no terminal line editing, history, completion or `'keypress'` event; on a terminal
the input is read in its normal (cooked) mode, so the terminal echoes and edits the line, and `rl.line` and
`rl.cursor` stay empty. No `'SIGINT'`/`'SIGTSTP'`/`'SIGCONT'` events; `emitKeypressEvents` does nothing.

## tty

| API | Notes |
|---|---|
| `isatty(fd)` | |
| `process.stdin` (`tty.ReadStream`) | `'data'` (Buffers, or strings after `setEncoding`), `'end'`, `'close'` (on a pipe or a terminal), `pause()`/`resume()`, `pipe()`, `for await`; `isTTY` (true, or `undefined` off a terminal), `setRawMode(mode)`, `isRaw` |
| `process.stdout`/`stderr` (`tty.WriteStream`) | `write`, `end`, `pipe()` target, `isTTY`, `columns`/`rows` (TIOCGWINSZ), `getWindowSize()`, `getColorDepth(env?)`/`hasColors(count?, env?)` with Node's rules (`FORCE_COLOR`, `NO_COLOR`, `TERM`, `COLORTERM`, `TMUX`, `CI`, `TERM_PROGRAM`) |

`process.stdin` works over a pipe, a file, `/dev/null` and a terminal. It is read on the event loop in 64 KiB
chunks, only while someone reads it (a `'data'` listener, `read()`, `pipe()`, `for await`, a readline interface):
a paused or untouched stdin does not keep the program alive, as in Node. The descriptor is not made non-blocking (it
is shared with the shell): a read comes only once the event loop says there is data.

Differences from Node: `process.stdin` is a `tty.ReadStream` whatever fd 0 is (Node: a `net.Socket` for a pipe, an
`fs.ReadStream` for a file); writes to `process.stdout`/`stderr` are synchronous (Node: asynchronous on pipes), and
their `end()` ends the stream object only (the descriptor stays open, `console.log` goes on); `pipe()` into
`process.stdout` ends that stream object when the source ends (Node leaves stdout open); there is no `'resize'`
event (`getWindowSize()` reads the size again); `new tty.WriteStream(fd)` also works on a descriptor that is not a
terminal.

## Default imports

`import fs from "node:fs"`, `import path from "path"` and the like (TypeScript's `esModuleInterop`) read as
`import * as fs from "node:fs"`: the default export of a Node module is the module itself. `node:events` is the
exception, as in Node: its default export is `EventEmitter`. The same holds for a package with no default export:
`import tracer from "dd-trace"` reads as `import * as tracer from "dd-trace"`; a package that has one
(`export default ...`) gives that.

## http

```ts
import http from "node:http";

const server = http.createServer((req, res) => {
  res.setHeader("Content-Type", "application/json");
  res.end(JSON.stringify({ method: req.method, url: req.url }));
});
server.listen(3000, () => console.log("listening"));
```

`createServer(listener)` gives a server whose listener receives an `IncomingMessage` and a `ServerResponse`:

- **Request:** `method`, `url` (path and query), `headers` (lower-case names), `rawHeaders`, `httpVersion`,
  `socket` (`remoteAddress`, `remotePort`, `localPort`, `encrypted`, `destroy()`), `complete`, `aborted`. The
  request (and the client's response) is a `stream.Readable`: `'data'`/`'end'`/`'close'`, `setEncoding`, `pipe()`
  and `for await (const chunk of req)`. Bodies are bytes: `'data'` gives Buffers (strings after `setEncoding`), so
  uploads of images or any binary arrive byte for byte; `body += chunk` gives the UTF-8 text, as in Node.
- **Response:** `statusCode`, `statusMessage`, `setHeader`/`appendHeader`/`getHeader`/`getHeaders`/`hasHeader`/
  `removeHeader`, `writeHead(status[, reason], headers)`, `flushHeaders()`, `write`, `end`, `headersSent`,
  `writableEnded`/`writableFinished`, and `'finish'`, `'close'` and `'drain'`. `write()` and `end()` take strings,
  Buffers and Uint8Arrays (gzip from `node:zlib`, files, downloads), and `Content-Length` counts bytes. The
  response is a `stream.Writable`, so a stream can `pipe()` into it.
- **Server:** `listen(port[, host], cb)`, with port 0 choosing a free port, `address()`, `close(cb)`,
  `closeIdleConnections()`, `closeAllConnections()`, `keepAliveTimeout` and the `'request'`, `'connection'`,
  `'checkContinue'`, `'listening'`, `'close'` and `'error'` events. `http.Server` works as a type and with `new`.

Responses stream as in Node. The head goes out with the first `write()` (or `flushHeaders()`), each `write()` is
sent at once, and the body is chunked when no `Content-Length` was set (`end(body)` alone sends a
`Content-Length`). Server-sent events work: `res.writeHead(200, { "Content-Type": "text/event-stream" })`, then
`res.write("data: ...\n\n")` from a timer. `write()` returns false once more than 16 KB wait to be sent, and
`'drain'` follows. When the client goes away, the request and the response get `'close'`.

A program can run any number of servers (an app and a metrics endpoint, the servers of a test), each on its own
port. `close()` of one leaves the others serving. `listen()` returns at once, as in Node. Each connection is a
task on the program's event loop, next to its timers and other sockets, and `close()` lets the program end
normally once its connections are closed.

The client side, `http.request(url | options, cb)` and `http.get(...)`, gives a `ClientRequest` (`setHeader`,
`write`, `end`, `setTimeout`, `'socket'`, `destroy`, `'response'`, `'error'`, `'close'`) and an `IncomingMessage` response
(`statusCode`, `statusMessage`, `headers`, `'data'`/`'end'`). The response's body comes as it arrives (a
server-sent events stream can be read), as Buffers. `STATUS_CODES` and `METHODS` are there too.

Connections go through an `http.Agent` (`https.Agent`), as in Node: `http.globalAgent` by default (keep-alive, as
Node's since v19), the `agent` option for another one, `agent: false` for a connection of the request's own.
`new Agent({ keepAlive, keepAliveMsecs, maxSockets, maxTotalSockets, maxFreeSockets, timeout, scheduling })`: with
`keepAlive` a connection whose response ended is used again by the next request to the same host and port
(`req.reusedSocket`) and the request says `Connection: keep-alive`, else `Connection: close`; `maxSockets` makes the
other requests to a host wait; `getName()`, `destroy()`, `totalSocketCount`. An idle connection does not keep the
program alive, and one the server closed is not used again.

Differences from Node:

- A client request's body is sent at `end()` with a `Content-Length` (Node streams it chunked when `write()` came
  before `end()`).
- `http.Agent`: no `sockets`/`freeSockets`/`requests` objects, no `createConnection`/`keepSocketAlive` hooks to
  override (so no proxy agents built on them), and `keepAliveMsecs` (TCP keep-alive probes) is not applied. An https
  connection is not reused: each https request has its own.
- A request body is not held back: `req.pause()` does not stop the reading of the socket, so a large upload the
  handler does not read is kept in memory.
- `headersTimeout`, `requestTimeout` and `server.setTimeout` are accepted but not enforced; idle keep-alive
  connections are closed after `keepAliveTimeout`. HTTP/1.1 only (no HTTP/2, no `upgrade`/`CONNECT` events).
- `req.socket` is a small socket object (addresses, `destroy()`, `'close'`), not a `net.Socket`. A client
  request's `'socket'` event (and its `req.socket`) gives a `net.Socket` whose `unref()`/`ref()` work: an unref'd
  request in flight does not keep the program alive, as an APM agent's send in Node. Its other methods and fields
  do not act on the request's connection.
- `new http.Server()` and `import { Server } from "node:http"` name node:http's server where they are imported;
  elsewhere `Server` is xisto's built-in server.

## net

`net.createServer((socket) => ...)` and `net.createConnection(port, host, cb)` / `net.connect` give TCP servers
and sockets:

- **Socket:** `'connect'`, `'data'` (Buffers, or strings after `setEncoding`), `'end'`, `'close'` (with `hadError`)
  and `'error'` (with Node's `code`: `ECONNREFUSED`, `ENOTFOUND`...). Methods `write(data, cb)`, `end(data)`,
  `destroy()`, and `unref()`/`ref()`: an unref'd socket does not keep the program alive while it waits for data
  (it still does while it connects or writes, as in Node). Fields `remoteAddress`/`remotePort`/`localAddress`/`localPort`, `bytesRead`/`bytesWritten`.
- **Server:** `listen(port, host, cb)` (port 0 picks a free one), `address()`, `close(cb)` (waits for the open
  connections), `getConnections`, `unref()`/`ref()`.
- `isIP`, `isIPv4`, `isIPv6`.

Each socket reads on a task of its own, so `'data'` arrives as the bytes do.

Differences from Node:

- IPv4 listeners only. No Unix domain sockets.
- No read backpressure (`pause()`).
- `write()` writes before it returns.
- The socket is an `EventEmitter`, not a `stream.Duplex`: `pipe(dest)` takes another `net.Socket` (an echo
  server's `c.pipe(c)`, a proxy), with `{ end }` and the `'pipe'` event, but no backpressure.
- `https.Server` extends `http.Server` (Node: `tls.Server`), so `instanceof http.Server` is true for it.

## dns

`dns.lookup(name, cb)`, `dns.resolve4` / `resolve6`, and `dns.promises.lookup(name, { family })` (plus
`lookupAll`), through the system resolver as Node's `lookup` does. A name that does not exist gives `ENOTFOUND`.

Differences from Node: `resolve4`/`resolve6` also use the system resolver (Node asks a DNS server). The record
queries (`resolveMx`, `resolveTxt`, `reverse`...) are not there yet. `promises.lookup(name, { all: true })` is
`promises.lookupAll(name)`.

## child_process

| API | Notes |
|---|---|
| `spawn(file, args, { cwd, env, shell })` | `stdout`/`stderr` give `'data'` as the child writes (`setEncoding` for text); `stdin.write`/`end`; `kill(signal)`; `unref()`/`ref()` (an unref'd child does not keep the program alive); `'spawn'`, `'exit'` (code, signal), `'close'`, `'error'` (`spawn x ENOENT`) |
| `exec(command, cb)`, `execFile(file, args, cb)` | `cb(error, stdout, stderr)`; a failure is Node's `Command failed: ...` error with `code` |
| `spawnSync(file, args, { input, cwd, encoding })` | `status`, `signal`, `stdout`, `stderr`, `error` |
| `execFileSync`, `execSync` | the output; a failure is thrown |

Differences from Node:

- stdio is always three pipes: no `'inherit'`, no extra descriptors, no `fork`/IPC.
- `stdout`/`stderr`/`stdin` are `EventEmitter`s, not streams.
- The synchronous calls return text: Node returns a Buffer when no `encoding` is given, so pass `{ encoding:
  "utf8" }` to have the same result on both.
- Not on Windows yet.
## Buffer

`Buffer` is a global and is exported by `buffer` / `node:buffer` / `xisto:buffer`, together with `isUtf8`, `isAscii`,
`atob`, `btoa`, `kMaxLength`, `kStringMaxLength`, `constants` and `INSPECT_MAX_BYTES`. A `Buffer` is a
`Uint8Array` (indexing, `length`, `subarray`, `set`, `for..of`) printed as Node prints it (`<Buffer 68 69>`, then
`... N more bytes` past 50).

Supported, with Node's results and Node's errors (`RangeError`, `TypeError` with `err.code`: `ERR_OUT_OF_RANGE`,
`ERR_BUFFER_OUT_OF_BOUNDS`, `ERR_INVALID_BUFFER_SIZE`, `ERR_INVALID_ARG_VALUE`, `ERR_UNKNOWN_ENCODING`):

- creating: `Buffer.alloc(size, fill?, encoding?)` (fill a byte, a string or bytes), `allocUnsafe`,
  `allocUnsafeSlow`, `from(string, encoding?)`, `from(int[] | number[])`, `from(bytes)`,
  `from(view.buffer, byteOffset?, length?)` (a view on the same memory), `of(...)`, `concat(list, totalLength?)`;
- `Buffer.compare`, `isBuffer`, `isEncoding`, `byteLength(string | bytes, encoding?)`, `Buffer.poolSize`;
- numbers at an offset: `readUInt8`, `readInt8`, `read[U]Int16LE/BE`, `read[U]Int32LE/BE`, `readBig[U]Int64LE/BE`,
  `readFloatLE/BE`, `readDoubleLE/BE`, `read[U]IntLE/BE(offset, byteLength)`, every matching `write*` (which
  answers the offset after the bytes written) and the `Uint` spellings (`readUint16LE`...);
- `toString(encoding?, start?, end?)`, `write(string, offset?, length?, encoding?)`, `equals`,
  `compare(target, targetStart?, targetEnd?, sourceStart?, sourceEnd?)`, `copy`, `fill(value, offset?, end?,
  encoding?)`, `indexOf` / `lastIndexOf` / `includes` (a string, bytes or a byte, a byte offset, an encoding),
  `slice` and `subarray` (views on the same bytes, as in Node), `swap16/32/64`, `toJSON`, `keys`, `values`,
  `entries`, `String(buf)` and `` `${buf}` ``, `Array.from(buf)`, `[...buf]`;
- encodings `utf8` / `utf-8`, `hex`, `base64`, `base64url`, `latin1` / `binary`, `ascii`, `ucs2` / `ucs-2` /
  `utf16le` / `utf-16le`, in any case. Invalid input behaves as in Node: hex stops at the first pair that is not
  hex, base64 skips whitespace and characters outside the alphabet (both alphabets are accepted), UTF-8 decoding
  replaces each ill-formed sequence with one U+FFFD as the WHATWG decoder does (`TextDecoder` too), and an odd
  last byte of UTF-16 is dropped.

Differences from Node:

- `Blob`, `File`, `transcode`, `isLatin1`, `resolveObjectURL` and `SlowBuffer` are missing; `INSPECT_MAX_BYTES`
  and `Buffer.poolSize` are constants (50 and 65536).
- `keys()`, `values()` and `entries()` return arrays, not iterators (the same in `for..of` and spreads;
  `console.log(buf.values())` prints the array).
- A string holds Unicode text, so UTF-16 decoding turns a lone surrogate into U+FFFD (printed, Node writes the
  same bytes), and `Buffer.byteLength(s, "latin1")` with a literal encoding counts code points (`s.length`), not
  UTF-16 units, for characters past U+FFFF.
- Offsets and lengths are `int`s; a `number` is accepted where it holds an integer (else Node's "It must be an
  integer" error). A non-number argument (`buf.readUInt8("1")`) is a compile error, not `ERR_INVALID_ARG_TYPE`.
- `err.code` is read through an `any` (`catch (e: any)`); `instanceof RangeError` on these errors is false, and
  `console.log(err)` prints `RangeError: message` without Node's `[code]` and `{ code }`.
- `Buffer.from(arrayBuffer)` works on `view.buffer` only (there is no `ArrayBuffer` class); `new Buffer()` is an
  error; `buf + ""` is an error (use `buf.toString()`); `import { Buffer as B }` is not supported.
## util

`node:util` (also `util`, `xisto:util`) has `format`, `formatWithOptions`, `inspect`, `isDeepStrictEqual`,
`promisify`, `callbackify`, `deprecate`, `debuglog` (and `debug`), `types`, `parseArgs`, `styleText`,
`stripVTControlCharacters`, `toUSVString`, `getSystemErrorName`, `isArray`, and the built-in `TextEncoder` and
`TextDecoder`. The default import (`import util from "node:util"`) is an object with the same functions.

- `inspect(value)` gives the text `console.log` prints for the value inside an object (a string in quotes), so the
  two always agree. The options `depth`, `breakLength` and `compact` (a number or `false`) change the layout as in
  Node. `colors`, `showHidden`, `sorted`, `maxArrayLength`, `maxStringLength`, `getters` and the others are
  accepted and ignored. `compact: true` is read as `3`. `depth: null` means the default depth (2), not "no limit",
  because a field left out and a field set to `null` are the same value; pass `Infinity` for no limit.
- `format` handles `%s %d %i %f %j %o %O %c %%` and appends the other arguments as Node does. `%o` is `%O` with depth
  4: it does not list the hidden `[length]` entries Node shows. `%s` of a function prints `[Function]`, not its
  source. `%s` of an object whose class declares `toString()` calls it, as Node does.
- `isDeepStrictEqual` compares numbers with `Object.is` (`NaN` equals `NaN`, `0` does not equal `-0`), strings,
  arrays element by element (an `int[]` and a `number[]` with the same values are equal), objects of the same class
  field by field, plain objects and records by key in any order, `Map` and `Set` entries in any order, `Date` by
  time, `Buffer` and `Uint8Array` by bytes, errors by name and message. A field left out of an interface object
  and a field set to `null` compare as equal (Node tells `undefined` from a missing key). Other built-in objects
  (a `RegExp`) are equal when they print the same.
- `promisify` and `callbackify` take a function of one argument plus the callback:
  `promisify(fn: (arg: A, cb: (err: Error | null, result: R) => void) => void)` gives `(arg: A) => Promise<R>`.
  TypeScript cannot give one generic signature for every number of arguments; `util.promisify.custom` is missing.
  `callbackify`'s callback gets `null` as the result on an error (Node: `undefined`).
- `deprecate(fn, msg, code?)` returns `(...args: any[]) => any`. The warning goes to stderr after the current
  synchronous code (Node: on the next tick, before promise callbacks), once per code, with Node's text.
  `process.noDeprecation` and `--no-deprecation` are not read; `NODE_NO_WARNINGS=1` is.
- `debuglog(section)` returns `(...args: any[]) => void` and reads `NODE_DEBUG` (comma-separated names, `*`
  wildcards). `debuglog(...).enabled` is missing.
- `types` has `isDate`, `isRegExp`, `isMap`, `isSet`, `isNativeError` (also a class that extends `Error`),
  `isPromise`, `isUint8Array` (also a `Buffer`), `isFloat64Array`, `isInt32Array`, `isTypedArray`,
  `isArrayBufferView`, `isSharedArrayBuffer`, `isAnyArrayBuffer`. The checks for kinds of value xisto does not
  have (`isArrayBuffer`, `isProxy`, `isWeakMap`, boxed primitives...) return `false`. `node:util/types` cannot be
  imported; use `types` from `node:util`.
- `parseArgs` follows Node's algorithm and error messages (`err.code` is `ERR_PARSE_ARGS_UNKNOWN_OPTION`,
  `ERR_PARSE_ARGS_INVALID_OPTION_VALUE` or `ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL`), with `strict`,
  `allowPositionals`, `allowNegative`, `tokens`, short and long options, groups (`-abc`), `string` and `boolean`
  types, `multiple` and `default`. `values` is a `Record<string, any>`: `console.log` prints it as a plain object,
  without Node's `[Object: null prototype]`. A token's missing `name`, `value` or `inlineValue` is `null`.
- `styleText(format, text, { validateStream })` has Node's styles and aliases and its error for an unknown one.
  With `validateStream` (the default) it styles only when stdout is a terminal (and `NO_COLOR`,
  `NODE_DISABLE_COLORS` and `TERM=dumb` are not set) or `FORCE_COLOR` asks for colors; the `stream` option is
  ignored (stdout is always the one tested).
- `getSystemErrorName(-n)` names the errors of `errno.h` that libuv maps (`ENOENT`, `EACCES`, `ECONNREFUSED`...);
  the names are those of the system xisto runs on.
- `toUSVString` replaces lone surrogates with U+FFFD; xisto strings are UTF-8, so they rarely hold one.
- Missing: `inherits`, `inspect.custom`, `inspect.defaultOptions`, `promisify.custom`, `MIMEType`, `parseEnv`,
  `aborted`, `getCallSite`, the deprecated `util.isX` functions other than `isArray`.

## querystring

`node:querystring` (also `querystring`, `xisto:querystring`) has `parse`/`decode`, `stringify`/`encode`, `escape`,
`unescape` and `unescapeBuffer`, with Node's algorithm: separators and equals signs of any length, `maxKeys` (1000
by default, `0` for no limit), `+` read as a space, a key given more than once collected into an array, a malformed
`%` sequence kept as written and bytes that are not UTF-8 read as U+FFFD. The options `decodeURIComponent` and
`encodeURIComponent` take your own functions.

- `parse` returns a `Record<string, any>` whose values are strings or string arrays; `console.log` prints it without
  Node's `[Object: null prototype]` prefix.
- `stringify` takes any object and reads its fields through `any` (strings, numbers, booleans, bigints and arrays of
  them; other values are written as empty, as in Node).

## string_decoder

`node:string_decoder` (also `string_decoder`, `xisto:string_decoder`) has `StringDecoder` with the encodings
`utf8` (`utf-8`), `utf16le` (`ucs2`, `ucs-2`, `utf-16le`), `latin1` (`binary`), `base64`, `base64url`, `hex` and
`ascii`. `write(bytes)` returns the text of the complete characters and keeps an incomplete one for the next call;
`end(bytes?)` returns what is left. It follows Node's algorithm (src/string_decoder.cc): an invalid UTF-8 sequence
becomes one U+FFFD per maximal invalid part, an incomplete UTF-8 character at `end()` one U+FFFD, and base64 holds
back the bytes of an incomplete group of three.

- `write` and `end` take a `Buffer` or a `Uint8Array`; Node also takes a string.
- A chunk that ends between the two halves of a UTF-16 surrogate pair at an odd byte makes Node return lone
  surrogates; xisto strings are UTF-8 and cannot hold one, so each half comes out as U+FFFD. A pair split at an
  even byte (the usual case) is joined as in Node.
- `lastChar`, `lastNeed` and `lastTotal` are missing.

## JSON

`JSON.stringify(value)` is generated for the value's type (the fast path, unchanged). `JSON.stringify(value,
replacer, space)` writes what Node writes:

- `space`: a number of spaces (clamped to 0..10) or a string (its first 10 characters); nested objects and arrays
  one member per line, `": "` after keys, empty `{}` and `[]` compact;
- `replacer`: `null`/`undefined`, a `string[]` of the keys to keep (at every level, in the list's order), or a
  function `(key: string, value: any) => any`, called first with the key `""` for the root, then for each member
  and element; returning `undefined` leaves a member out (`null` in an array), a new object or array returned is
  walked in turn;
- `toJSON`, `Date` and optional fields left out behave as in the one-argument call.

`JSON.parse(text, reviver)` calls `reviver(key, value)` from the leaves up, the root last with the key `""`;
`undefined` deletes the member. Into an `any` the revived value is kept as the reviver made it (a `Date` stays a
`Date`); into a declared type, the revived JSON is decoded as `JSON.parse(text)` decodes it.

```ts
console.log(JSON.stringify(user, ["id", "email"], 2));
console.log(JSON.stringify(user, (k, v) => (k === "password" ? undefined : v), "\t"));
const ev: any = JSON.parse(text, (k, v) => (k === "at" ? new Date(v) : v));
```

Differences from Node:

- The replacer sees the value as JSON gives it back: after `toJSON` (as in Node: a `Date` is already its ISO
  string), but a class instance as a plain object (`Record<string, any>`), where Node passes the instance itself.
  `value instanceof MyClass` is `false` there. A `Map` or `Set` the replacer returns is written `{}`, as in Node.
- xisto has one empty value, so `undefined` in a replacer is recognized where the code says it: `return undefined`
  or `undefined` in a `?:` branch of a replacer written in place, or of a function declared with an `any` or
  `unknown` result passed by name (that function then returns JSON's `undefined` for every caller). Elsewhere
  `undefined` is `null` and is written as `null`.
- When the replacer drops the root, the result is the string `"undefined"` (Node: the value `undefined`).
- `this` inside a replacer is not the holder object.

### JSON and nulls

By default `JSON.stringify` writes a member that is `null` as `"k":null`, as Node does. A project can leave those
members out of every JSON it writes, in its `xisto.json`:

```json
{
  "name": "app",
  "main": "src/main.ts",
  "json": { "omitNull": true }
}
```

`xisto init` writes this option into a new project. With it, `JSON.stringify` leaves out the fields of objects and
the entries of a `Record` (or of an `any`) whose value is `null`, at every level; an array keeps its `null`
elements, and a `null` value itself is still `"null"`. The choice is made when compiling: the writers the compiler
generates test for `null` where a field can hold it, with no cost at run time. Because the JSON then differs from
Node's, the build says so once, with warning `XS2111` at the first `JSON.stringify`; remove the option (or set it to
`false`) to write Node's JSON.

The option also changes what the program sends to other services, which may treat `null` and a missing member
differently: in an Elasticsearch partial update, `{"doc":{"nick":null}}` clears the field, while a `doc` without
`nick` leaves it as it is. With `omitNull`, `JSON.stringify` cannot write the first form: keep the option off in a
project that must send `null` to clear a field, or write that body's text yourself (`` `{"doc":{"nick":null}}` ``).

Without the option, the usual replacer that leaves nulls out, `JSON.stringify(v, (k, v) => v ?? undefined)` (or
`(k, v) => v === null ? undefined : v`), is recognized and compiled the same way: no call per member, the same
output as Node's (`null` array elements stay `null`). One difference: `JSON.stringify(null, (k, v) => v ?? undefined)`
is `"null"` in xisto, the value `undefined` on Node.

## console.log and inspect

Strings inside objects are quoted as Node quotes them: `'...'`, or `"..."` when the string has a `'`, or `` `...` ``
when it has both; `\`, tabs, `\r` and other control characters are escaped (`\x1B`). A long string with line
breaks inside an object is printed one line per piece, joined by `+`, as Node does.

## console

`console.log`, `info`, `debug` (stdout), `error` and `warn` (stderr) are the compiler's own, as before. The rest is
`runtime/node/console.ts`, loaded only by a program that names one of them:

- `console.time(label?)`, `timeLog(label?, ...data)`, `timeEnd(label?)`: `label: 1.234ms` in Node's format
  (`1.500s`, `1:02.345 (m:ss.mmm)` for longer times);
- `console.count(label?)` (`default: 1`), `countReset(label?)`;
- `console.group(...label)`, `groupCollapsed`, `groupEnd`: everything printed by `console.log`, `info`, `debug`,
  `error`, `warn`, `dir`, `table`, `count`, `timeEnd`... inside a group is indented by two spaces per level, each
  line of a multi-line text too;
- `console.table(data, columns?)`: Node's box-drawing table for arrays of objects, of primitives and of arrays,
  and for objects of objects (the `(index)` column, a `Values` column for primitives, the `columns` filter);
  cells are inspected as Node does (depth 0, at most three array items, an object of more than two keys as
  `[Object]`), and wide East Asian characters count as two columns; anything else is printed as `console.log`;
- `console.dir(value, { depth })` (`depth: null` for every level), `dirxml`;
- `console.assert(value, ...message)`: `Assertion failed` or `Assertion failed: message` to stderr;
- `console.trace(...message)`: `Trace: message` and the stack to stderr.

Differences from Node:

- `console.log` keeps its emitted code and its stdout fast path. Only a program that names `console.group` or
  `console.groupCollapsed` emits each `console.log` between a begin/end pair that, inside a group, prints the
  line into memory and writes it indented: a program that never groups pays nothing;
- `console.trace` prints xisto's stack frames (`at file.ts:3:5`), not Node's;
- `console.table` takes arrays and objects; `Map`, `Set` and iterators print as `console.log` does. An array of
  objects of different shapes must be typed (`const rows: any[] = [...]`); its values take the dynamic path;
- the warnings for a label that is not there (`timeEnd`, `timeLog`, `countReset`) and for a `time` label used
  twice are written as Node's process warnings (`(node:PID) Warning: ...`, after the current task), without a
  `'warning'` event on `process`.

## performance

`performance.now()` is the compiler's own (one clock read). The rest of User Timing is
`runtime/node/perf_hooks.ts`, also through `import { performance } from "node:perf_hooks"` (the same global
object; importing only `performance` loads nothing):

- `performance.mark(name, { detail, startTime }?)` returns a `PerformanceMark` (`name`, `entryType: "mark"`,
  `startTime`, `duration: 0`, `detail`);
- `performance.measure(name, startMark?, endMark?)` or `measure(name, { start, end, duration, detail })` (marks by
  name or times) returns a `PerformanceMeasure`; a mark that was not set throws `The "x" performance mark has not
  been set`;
- `getEntries()` (in `startTime` order), `getEntriesByName(name, type?)`, `getEntriesByType(type)`,
  `clearMarks(name?)`, `clearMeasures(name?)`, `timeOrigin` and `toJSON()`;
- `PerformanceEntry`, `PerformanceMark`, `PerformanceMeasure` from `node:perf_hooks`, for `instanceof`.

Differences from Node: the clock starts at the program's start (Node's `now()` also counts its own startup, some
tens of milliseconds); `toJSON()` gives `{ timeOrigin }` only; the missing-mark error is a `SyntaxError` (Node: a
`DOMException` named `SyntaxError`); `performance` is not a value (it cannot be passed around or compared); no
`PerformanceObserver`, `timerify`, `eventLoopUtilization`, `nodeTiming` or resource timing.

## globalThis and the global crypto

`globalThis.X` reads the standard global `X` (`globalThis.setTimeout(...)`, `globalThis.crypto.randomUUID()`,
`globalThis.process.platform`, `new globalThis.URL(...)`): it is read as `X`. The feature checks are constants
known when the program is compiled, with Node's answers:

```ts
typeof globalThis.fetch !== "undefined"; // true
typeof globalThis.Deno; // "undefined"
"Bun" in globalThis; // false
typeof window === "undefined"; // true: window, document, self, Deno, Bun... are not Node's globals
```

`globalThis` holds the fixed set of Node's globals (functions, constructors, `console`, `process`, `crypto`,
`performance`, `Math`, `JSON`...). What would make it a dynamic object is an error that says so: assigning a
property (`globalThis.config = ...`, also for a known global: a polyfill), reading a name that is not a global, and
`globalThis` as a value. A value shared across modules is an exported module variable (`export let config`).
`typeof` and `in` answer for every Node global, also the ones xisto does not provide (`WebSocket`); using one of
those still fails where it is used.

The global `crypto` is Web Crypto, as in Node 20+: `crypto.randomUUID()`, `crypto.getRandomValues(bytes)` and
`crypto.subtle` (`digest` with SHA-1/256/384/512, HMAC `importKey`/`sign`/`verify`/`exportKey`), the same object as
`import { webcrypto } from "node:crypto"`; a module that imports or declares (at its top level) its own `crypto`
keeps it, but a parameter or local variable named `crypto` whose `randomUUID`, `getRandomValues` or `subtle` is
read is taken for the global: rename it. As with
node:crypto's `subtle`, `digest` and `sign` give a `Buffer` (Node: an `ArrayBuffer`); `getRandomValues` takes a
`Uint8Array`; `subtle` has no RSA, ECDSA or AES keys (see [crypto](#crypto)).

## assert

`node:assert` and `node:assert/strict` (`runtime/node/assert.ts`, `assert_strict.ts`), imported as a default (the
module, also callable: `assert(value)`), as a namespace, by name, or as `import { strict as assert } from
"node:assert"`:

- `assert(value, message?)` / `ok`, `equal`, `notEqual`, `strictEqual`, `notStrictEqual`, `deepEqual`,
  `notDeepEqual`, `deepStrictEqual`, `notDeepStrictEqual`, `match`, `doesNotMatch`, `fail`, `ifError`;
- `throws(fn, expected?, message?)` and `rejects(asyncFn, ...)` with a class (`TypeError`, a class of the
  program), a `RegExp` (tested against `String(error)`), a validation function, an object whose properties the
  error must have (compared with `deepStrictEqual`), or a message; `doesNotThrow` and `doesNotReject` (an error
  of another class than the one given is thrown again);
- `AssertionError` (`assert.AssertionError` too): `name`, `code: "ERR_ASSERTION"`, `actual`, `expected`,
  `operator`, `generatedMessage`; `new AssertionError({ actual, expected, operator, message })`.

The messages are Node's: `assert(x > 2)` says `The expression evaluated to a falsy value:\n\n  assert(x > 2)\n` (the
compiler passes the call's source text), `strictEqual(1, 2)` says `Expected values to be strictly equal:\n\n1 !== 2\n`,
longer values the stacked `+ actual - expected` form with Node's caret, and objects and arrays Node's line diff
(Myers, with `...` for skipped lines); `deepEqual`, the `not` forms, `throws`' class, RegExp, validation and
object checks, `Missing expected exception.`, `Got unwanted exception.` all as Node writes them.

Differences from Node:

- the messages show an object's keys in declaration order (Node sorts them); `undefined` reads as `null`;
- an uncaught `AssertionError` is printed in xisto's format (`uncaught Error: Expected values...` and the stack;
  Node prints the error object with its properties); the exit status is 1 in both;
- `deepEqual` compares primitives with `==` and objects by their own keys, without Node's other loose rules
  (prototypes are not compared in either); in an object given to `throws`, a `RegExp` value is compared as a
  value, not tested against the string;
- `assert.strict.x(...)` (a member of a member) is not supported: import `node:assert/strict`, or `{ strict }`;
  `fail(error)` takes an `Error` (not an instance of a subclass); the messages are `string`s (an `Error` as the
  message of `ok`/`equal`... is thrown as it is);
- no `CallTracker`, `partialDeepStrictEqual` or `assert.Assert` instances.

## test

`node:test` (`runtime/node/test.ts`): a program that registers tests runs them after its module body, one at a
time, and prints Node's default reporter (spec) to stdout, as `node file.ts` does; the exit status is 1 when a test
fails.

- `test(name, fn)`, `test(name, options, fn)`, `it`, `describe`/`suite` (the suite's body runs at once and
  registers its tests and hooks), `before`, `after`, `beforeEach`, `afterEach` (in Node's order, inner suites'
  `beforeEach` after the outer ones');
- `test.skip`, `test.todo`, `test.only`, `it.skip`..., `describe.skip`..., and the options `{ skip, todo, only }`
  (`true` or a reason); with `only` in a file, the other tests of that level do not run and are not reported, as
  Node 26 does without `--test-only`;
- async tests, subtests (`await t.test(name, fn)`), `t.name`, `t.diagnostic(message)`, `t.skip(message?)`,
  `t.todo(message?)`;
- the output: `✔ name (0.5ms)`, `✖`, `﹣ ... # SKIP`, `# TODO`, `⚠` for a todo that throws, `▶ suite` headers with
  the children indented, `ℹ` diagnostics, the summary (`ℹ tests`, `suites`, `pass`, `fail`, `cancelled`,
  `skipped`, `todo`, `duration_ms`) and the `✖ failing tests:` section (`test at file.ts:12:1`, the result line and
  the error).

`xisto test` builds and runs each `*.test.ts` as a program and reports it as failed when it exits with an error:
a file that uses `node:test` (built with `--node-compat`, or importing `xisto:test`) fails there when one of its
tests fails, and its output is this reporter's.

Differences from Node:

- the errors in the failing-tests section show xisto's stack frames (`at file.ts:9:10`) and, for an
  `AssertionError`, its properties with `diff: 'simple'`; an error of a class that extends `Error` is shown
  without its own extra properties;
- the reporter's lines are held until the program first waits on the event loop (all of them at the end when no
  test waits on a timer or I/O), then written as each test ends; Node's reporter stream can place them
  differently around a test's own `console.log` output;
- `timeout`, `concurrency` and `signal` are accepted and not applied; no `mock`, `t.plan`, `t.assert`, `t.mock`,
  snapshot tests, `run()`, reporters other than spec, or the done-callback form; subtests must be awaited;
- a test function's result is awaited when it is a promise; any other value is ignored.

## diagnostics_channel

`node:diagnostics_channel` has `channel(name)` (`subscribe`, `unsubscribe`, `publish`, `hasSubscribers`),
`subscribe`, `unsubscribe` and `hasSubscribers`. `node:http` and `fetch` publish to Node's channels, with Node's
message shapes:

- `http.server.request.start` / `http.server.response.finish` (`request`, `response`, `server`);
- `http.client.request.start` / `http.client.response.finish` / `http.client.request.error`;
- `undici:request:create` / `undici:request:headers` / `undici:request:error` for `fetch`. The request has
  `origin`, `method`, `path` and `addHeader(name, value)`, which adds a header before the request is sent.

APM agents use these channels to trace incoming and outgoing HTTP; the xisto-apm package subscribes to them. A
channel nobody listens to costs one test of a boolean.

Differences from Node: the class is `DiagnosticsChannel`, because `Channel` is xisto's channel between tasks.
`TracingChannel` is not available yet.

## https and tls

`https.createServer({ key, cert }, listener)` is `http.createServer` over TLS. `key` and `cert` are PEM text or
Buffers, as in Node. `https.request`/`get` make https requests. The `ca` option trusts one more CA certificate,
on top of the system's and `NODE_EXTRA_CA_CERTS`.

`tls.connect({ host, port, servername, ca }, cb)` gives a `TLSSocket`:

- `'secureConnect'`, `authorized`, `'data'` (with `setEncoding`), `'end'`, `'close'` and `'error'`;
- `write`, `end` and `destroy`.

The certificate is always verified, with AWS-LC (the default) and with BearSSL (`--tls=bearssl`). Importing
`node:https` or `node:tls` links TLS into the program; `node:http` alone does not.

Differences from Node:

- `rejectUnauthorized: false` is refused with an error, instead of making an unverified connection. Pass the
  server's CA as `ca`.
- `ca` adds to the CAs the whole program trusts, for the connections made after it. In Node it applies to that
  connection only.
- The handshake's error messages are the TLS library's, not OpenSSL's codes (`DEPTH_ZERO_SELF_SIGNED_CERT`...).
- Not available yet: `tls.createServer` (`node:https` serves TLS), client certificates, `passphrase`, SNI
  callbacks. `node:https` has node:http's behavior otherwise: binary bodies, streaming, several servers.
- `TLSSocket` is an `EventEmitter`, not a `stream.Duplex`.

## TLS

TLS is linked only when the program can use https: an `https://` URL, a `fetch` URL known only at run time, or
`listen(port, { cert, key })`. A program that only calls `http://` URLs carries no TLS code. The build flags
`--tls=off` and `--tls=bearssl` choose otherwise (see the [CLI reference](../reference/cli.md)).

## zlib

`node:zlib` (also `zlib`, `xisto:zlib`) is zlib 1.3.1 and Brotli 1.2.1 (the version Node 26 links) themselves,
compiled from `third_party/zlib` and `third_party/brotli` into static libraries that are linked only into programs
that import the module, and then only for what they call: zlib is about 50 KB of code, Brotli's decoder about
170 KB (most of it its dictionary) and its encoder about 600 KB more. The binaries do not depend on the system's
libz or libbrotli.

Supported:

- `deflateSync`/`inflateSync`, `gzipSync`/`gunzipSync`, `deflateRawSync`/`inflateRawSync`, `unzipSync`, and the
  callback forms `deflate(buf, [options], cb)` and the others;
- `brotliCompressSync`/`brotliDecompressSync` and `brotliCompress`/`brotliDecompress`, with `params`
  (`{ [constants.BROTLI_PARAM_QUALITY]: 4 }`: mode, quality, window, block size, size hint...), `chunkSize`,
  `finishFlush`, `maxOutputLength`, and Node's errors (`Decompression failed` with `code`
  `ERR__ERROR_FORMAT_...` and the decoder's `errno`, `ERR_BROTLI_INVALID_PARAM`);
- the streams `createGzip`, `createGunzip`, `createDeflate`, `createInflate`, `createDeflateRaw`,
  `createInflateRaw`, `createUnzip`, `createBrotliCompress`, `createBrotliDecompress` (Transforms of
  `node:stream`: `pipe`, `write`/`end`, `'data'`, `'error'`);
- `crc32(data, value?)`, `constants` (flush values, return codes, levels, strategies, modes, option bounds);
- options `level`, `windowBits`, `memLevel`, `strategy`, `dictionary`, `finishFlush` (`Z_SYNC_FLUSH` reads a
  truncated stream), `maxOutputLength`, `chunkSize`;
- Node's errors: `Error` with `code` (`Z_DATA_ERROR`, `Z_BUF_ERROR`, `Z_NEED_DICT`) and `errno`, and the
  `RangeError`s of bad options (`ERR_OUT_OF_RANGE`, `ERR_BUFFER_TOO_LARGE`) with Node's messages;
- gunzip reads gzip members one after the other and ignores trailing zero bytes, as Node.

The compressed bytes are Node's, byte for byte, for every level, strategy, `windowBits`, `memLevel` and `chunkSize`
(tested against Node built with the stock zlib; `tests/ok/node-zlib-bytes.ts`), and so are Brotli's for every
quality and parameter (`tests/ok/node-zlib-brotli.ts`). The output is produced
`chunkSize` bytes at a time as Node does, which matters for level 0. The gzip header's OS byte is zlib's for the
platform (3 on Linux, 19 on macOS), as in Node. Official Node binaries bundle Chromium's fork of zlib, which
does not promise the same bytes as zlib: against such a Node the output always decompresses to the same data, but
its bytes may differ.

Differences from Node:

- the input is a string (UTF-8) or a `Uint8Array`/`Buffer`; `ArrayBuffer` and `DataView` are not accepted;
- the callback forms compress at once and call back on a later turn of the event loop (Node runs them on its
  thread pool); on an error the result argument is an empty `Buffer` (Node: `undefined`);
- the `RangeError`s are `Error`s named `RangeError` (`instanceof RangeError` is `false`);
- Brotli `params` values are numbers (Node also takes `true`/`false`);
- the streams keep what is written and compress or decompress it when the writable side ends: the output is one
  chunk, with Node's bytes, and nothing comes out before the end (Node streams as it goes); the classes are one,
  `ZlibStream` (Node: `Gzip`, `Deflate`...);
- missing: Zstd, `params()`,
  `flush()`, the `info` option, and `zlib.promises`-style use without `util.promisify`.

## crypto

`node:crypto` (also `crypto`, `xisto:crypto`). Hashes, HMAC, the key derivations and Ed25519 are xisto's own C;
ciphers, RSA and EC are BearSSL's, linked with the module as a static library (a program takes only the functions it
calls: one that only hashes carries no BearSSL code).

Supported:

- `createHash` (md5, sha1, sha224, sha256, sha384, sha512, also by OpenSSL's names: `SHA256`, `RSA-SHA256`,
  `sha-256`), `update` of text or bytes, `digest()` (a `Buffer`) or `digest(encoding)`, `copy()`; `crypto.hash`;
  `createHmac` with a text, `Buffer` or secret `KeyObject` key; `getHashes`, `getCiphers`, `getCurves`;
- `randomUUID`, `randomBytes` (a `Buffer`), `randomInt`, `randomFillSync`, `getRandomValues`, `timingSafeEqual`;
- `createCipheriv`/`createDecipheriv`: `aes-128/192/256` in `cbc`, `ctr`, `gcm` and `ecb`; `update(data,
  inputEncoding?, outputEncoding?)` and `final(outputEncoding?)` (text output across calls is kept whole, as
  Node's string decoder: a UTF-8 character or a base64 group cut between calls waits for the next);
  `setAutoPadding`, and for GCM `setAAD`, `getAuthTag`, `setAuthTag`, `authTagLength`, any IV length;
- `pbkdf2Sync`/`pbkdf2`, `scryptSync`/`scrypt` (`N`/`cost`, `r`/`blockSize`, `p`/`parallelization`, `maxmem`),
  `hkdfSync`/`hkdf`;
- keys: `createPublicKey`, `createPrivateKey` and `createSecretKey`; `KeyObject` with `type`,
  `asymmetricKeyType`, `asymmetricKeyDetails`, `symmetricKeySize`, `equals`, and `export` to PEM
  (`spki`, `pkcs1`, `pkcs8`, `sec1`), DER (`format: "der"`) and JWK (`format: "jwk"`). Imports read PEM (`PUBLIC
  KEY`, `RSA PUBLIC KEY`, `PRIVATE KEY`, `RSA PRIVATE KEY`, `EC PRIVATE KEY`), DER with its `type`, and JWK; a
  private key's PEM gives `createPublicKey` its public half;
- `generateKeyPairSync("rsa", { modulusLength, publicExponent })` (512 to 4096 bits) and `generateKeyPairSync("ec",
  { namedCurve })` (`prime256v1`/`P-256`, `secp384r1`/`P-384`, `secp521r1`/`P-521`), `generateKeyPairSync("ed25519")`;
  with `publicKeyEncoding` and
  `privateKeyEncoding` the keys come as PEM text, as Node; `generateKeySync("hmac" | "aes", { length })`;
- signatures: `createSign`/`createVerify` (`update`, `sign(key, encoding?)`, `verify(key, signature,
  encoding?)`) and one-shot `sign`/`verify`, with RSA PKCS#1 v1.5, RSA-PSS (`padding:
  constants.RSA_PKCS1_PSS_PADDING`, `saltLength`), ECDSA (`dsaEncoding: "der"` or `"ieee-p1363"`) and Ed25519
  (`sign(null, data, key)`, as Node: not through `createSign`); keys as PEM text, `KeyObject` or `{ key, ... }`;
- `publicEncrypt` (RSA PKCS#1 v1.5 and OAEP) and `constants`;
- `webcrypto` / `subtle`: `digest` (SHA-1/256/384/512), HMAC keys (`importKey` from `raw` or a `jwk` of kty
  `oct`, `exportKey("raw")`), `sign` and `verify`, as `jose`-style code uses them; `webcrypto.getRandomValues` and
  `randomUUID`.

Byte for byte with Node (tested): hashes and HMACs, every cipher with a fixed key and IV (and GCM's tags),
pbkdf2/scrypt/hkdf, RSA PKCS#1 v1.5 and Ed25519 signatures, PEM/DER/JWK of a key read and written back, and the
error messages and codes. ECDSA and RSA-PSS signatures are random in both (BearSSL's ECDSA uses RFC 6979 nonces), so
they are tested by verifying each implementation's signatures with the other's keys.

Differences from Node:

- result types follow the call as written: `digest()`, `update(data)`/`update(data, inputEncoding)`, `final()`,
  `sign(key)` and `export({ format: "der" })` give a `Buffer`; with an output encoding (or `format: "pem"`) text;
  `export({ format: "jwk" })` a `JsonWebKey`. The options must be an object literal at the call for `export` and
  for `generateKeyPairSync`'s encodings to decide the type;
- `hkdfSync` and `subtle`'s `digest`, `sign` and `exportKey` give a `Buffer` (Node: an `ArrayBuffer`;
  `Buffer.from(result)` works with both);
- the async forms (`pbkdf2`, `scrypt`, `hkdf`) compute at once and call back on a later turn of the event loop;
  on an error the result is an empty `Buffer`;
- errors are `Error`s with Node's `code` and `name` (`TypeError`, `RangeError`), not instances of those classes;
  hash and HMAC errors are plain `Error`s with Node's message;
- `asymmetricKeyType` of a secret key is `null` (Node: `undefined`); an Ed25519 key's JWK lists `crv` after `x`
  in `JSON.stringify` (Node: before);
- missing: Ed448, X25519/X448, DSA and DH, encrypted private keys (`passphrase`), keys from
  certificates, `privateDecrypt`/`privateEncrypt`/`publicDecrypt`, `createECDH`/`diffieHellman`, other
  ciphers (`chacha20-poly1305`, `aes-*-ccm`, `-ocb`, `-cfb`...), `Cipher`/`Hash` as streams (`pipe`), the async
  `generateKeyPair`, `X509Certificate`, and `subtle` beyond HMAC and digests (RSA, ECDSA, AES keys, `deriveBits`,
  `exportKey("jwk")`).

## fs, path, os, url

**`node:fs`, synchronous**: `readFileSync`, `writeFileSync`, `appendFileSync` (with an encoding or an options
object: `{ encoding, flag, mode }`), `existsSync`, `unlinkSync`, `mkdirSync` (`{ recursive }`: gives the first
directory made), `readdirSync` (`{ withFileTypes }`: `Dirent`s with `name`, `parentPath`, `isFile()`,
`isDirectory()`, `isSymbolicLink()`...; `{ recursive }`), `statSync`/`lstatSync`/`fstatSync` (`Stats`: `size`,
`mode`, `nlink`, `uid`, `ino`..., `atimeMs`/`mtimeMs`/`ctimeMs`/`birthtimeMs` and their `Date`s, `isFile()`,
`isDirectory()`, `isSymbolicLink()`...; `{ throwIfNoEntry: false }`), `rmSync` (`{ recursive, force }`),
`rmdirSync`, `renameSync`, `copyFileSync` (`COPYFILE_EXCL`), `cpSync` (`{ recursive, force, errorOnExist,
filter }`), `mkdtempSync`, `realpathSync`, `accessSync`, `chmodSync`, `symlinkSync`, `readlinkSync`, `linkSync`,
`truncateSync`, `utimesSync`/`lutimesSync`, `openSync`/`closeSync`/`readSync`/`writeSync`, `globSync` (a pattern or
several, `cwd`, `exclude` as patterns or a function of the path, `withFileTypes`; Node's results and order), and `fs.constants` (`F_OK`, `R_OK`,
`W_OK`, `X_OK`, `COPYFILE_EXCL`, `S_IF*`...). Errors are Node's: the message (`ENOENT: no such file or directory,
open '/x'`, `EEXIST: file already exists, mkdir '/x'`, `rename 'a' -> 'b'`...) and `err.code`, `errno` (this
system's, negative), `syscall`, `path` and `dest`, read through an `any` (`catch (e: any)`) or a cast; `rmSync` of
a directory without `recursive` is Node's `SystemError` `ERR_FS_EISDIR`.

**Callbacks**: `fs.readFile(path, options?, callback)` (`"utf8"` or `{ encoding }`: text; otherwise a `Buffer`),
`writeFile`, `appendFile`, `mkdir`, `readdir` (`withFileTypes`), `stat`, `lstat`, `fstat`, `rm`, `rmdir`, `rename`,
`access`, `unlink`, `copyFile`, `cp`, `realpath`, `mkdtemp`, `readlink`, `symlink`, `link`, `chmod`, `truncate`,
`utimes`, and on file descriptors `open`, `read`, `write`, `close` (and `exists(path, cb)`): the work and the
callback happen on a later turn of the event loop, `err` is `null` when it worked. `fs.glob(pattern, options?, cb)`
and `fs.promises.glob(pattern, options?)` (an async iterator) give `globSync`'s matches.
`fs.watch(path, options?, listener?)` gives an `FSWatcher` (`'change'` with `(eventType, filename)`, `close()` and
`'close'`, `ref()`/`unref()`, `{ persistent: false }`): a name created, removed or renamed in a watched directory is
`"rename"`, a write `"change"`. Linux uses inotify, as Node; macOS uses kqueue where Node uses FSEvents, so the event
types can differ there (FSEvents reports a write as `"rename"` at times, and what happened just before the watcher
started). `recursive` is accepted, but only the directory's own entries are watched.

**Promises**: `node:fs/promises` and `fs.promises` (also `import { promises } from "node:fs"`): `readFile`
(text with an encoding, else a `Buffer`), `writeFile`, `appendFile`, `unlink`, `mkdir`, `readdir`, `stat`,
`lstat`, `rm`, `rmdir`, `rename`, `copyFile`, `cp`, `mkdtemp`, `realpath`, `access`, `chmod`, `readlink`,
`symlink`, `link`, `utimes`, `lutimes`, `truncate`.

The callback API, `fs.promises` and the streams are loaded only by programs that name them (as `fs.readFile`,
`import { readFile }`...): a program that only calls `readFileSync` does not carry the event loop.

**`node:path`**: POSIX `join`, `resolve`, `normalize`, `relative`, `dirname`, `basename`, `extname`,
`isAbsolute`, `parse`, `format`, `toNamespacedPath`, `sep`, `delimiter`; `path.posix` (the same) and
`path.win32` (`normalize`, `join`, `resolve`, `relative`, `isAbsolute`, `dirname`, `basename`, `extname`, `parse`,
`format`, `toNamespacedPath`, `sep`, `delimiter`, with drive letters and UNC roots as Node's), also as the modules
`node:path/posix` and `node:path/win32`.

**`node:os`**: `platform`, `type`, `arch`, `machine`, `release`, `version`, `hostname`, `homedir`, `tmpdir`,
`endianness`, `EOL`, `devNull`, `uptime`, `loadavg`, `totalmem`, `freemem`, `availableParallelism`, `cpus`,
`userInfo` (`uid`, `gid`, `username`, `homedir`, `shell`), `networkInterfaces` (`address`, `netmask`, `family`,
`mac`, `internal`, `cidr`, `scopeid`) and `os.constants` (`signals` and `errno` with this system's numbers,
`priority`).

**`node:url`**: the legacy `url.parse` (DEP0169 printed once, as Node does), `url.format` and `url.resolve`, and
`fileURLToPath(url)` and `pathToFileURL(path)`, encoded as Node encodes them (spaces, `%`, `#`, `?`,
`\`, non-ASCII), with Node's errors (`ERR_INVALID_URL_SCHEME`, `ERR_INVALID_FILE_URL_HOST`,
`ERR_INVALID_FILE_URL_PATH`).

**`import.meta`**: `import.meta.url` (a `file://` URL), `import.meta.filename` and `import.meta.dirname` are the
module's source file **as the build found it** (absolute, symbolic links resolved). Node gives the file it runs;
xisto compiles ahead of time, so the value is where the source was when it was built, not where the binary runs.
That is what `new URL("./data.json", import.meta.url)` and `fileURLToPath(import.meta.url)` want to find files
kept next to the source; a binary moved to another machine still names the build machine's paths (ship such
files next to the binary and find them with `process.execPath` instead). `import.meta.resolve` and
`import.meta.main` are not there.

Differences from Node:

- Paths are strings: not `Buffer`s or `URL`s (`fileURLToPath(url)` gives one), no file descriptors where a path
  goes (beyond `fstatSync`, `readSync`, `writeSync`, `open`/`read`/`write`/`close` and the streams).
- `readFileSync` takes `"utf8"`/`"utf-8"` (or `{ encoding }` with one) for text; another encoding is
  `readFileSync(path).toString(encoding)`. `writeFileSync`/`appendFileSync` write text in another encoding
  (`"latin1"`, `"base64"`...) when the encoding is written in the call (a literal; one held in a variable throws
  an error that says so). `openSync` takes the flags `r`, `w`, `a`, `r+`.
- On an error the callback's result is empty (`""`, an empty `Buffer` or array, a `Stats` of zeros, `-1` for
  `open`'s descriptor) where Node
  passes `undefined`; a missing optional value (`mkdirSync`'s result, `err.dest`) prints as `null` in
  `console.log`.
- `console.log(err)` prints `Error: <message>` (no `{ errno, code, syscall, path }` block and no stack);
  `console.log(stats)`/`console.log(dirent)` print xisto's form of the object; `os.userInfo()` and
  `os.constants.priority` print without `[Object: null prototype]`.
- `fs.promises` calls run when awaited, on the calling thread (Node: its thread pool), and the callback API
  runs the call on a later turn of the event loop (Node: in parallel).
- Not there: `fs.watch`/`watchFile`, `fs.glob`/`globSync`, `opendirSync`/`Dir`, `FileHandle`
  (`fs/promises` `open`), `fs.read`/`write` on descriptors with callbacks, `statfs`, `bigint` stats,
  `path.matchesGlob`, `path.win32.resolve`/`relative` (and `toNamespacedPath` of a relative Windows path, which
  Node resolves against the current directory first), `os.setPriority`/`getPriority`, `os.cpus()` models and
  speeds (empty and 0).

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
