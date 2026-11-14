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
- `http`: Node's `createServer`/`request`/`get` (below), and xisto's own `Server` with `fetch`, with TLS for
  `https://` (AWS-LC by default, or BearSSL)
- `events` (`EventEmitter`), `process`, `timers`, `http`, `net`, `dns`, `child_process` (see below)
- `util`, `querystring`, `string_decoder` (below)
- `http` / `https`: a server and `fetch`, with TLS (AWS-LC by default, or BearSSL)
- `crypto`: hashes, HMAC, AES ciphers, pbkdf2/scrypt/hkdf, keys and RSA/ECDSA signatures (see [crypto](#crypto))
- `zlib`: deflate, gzip, raw deflate and Brotli (see [zlib](#zlib))
- `stream`, `stream/promises`, `stream/web`, and `fs.createReadStream`/`createWriteStream` (see [stream](#stream))
- `async_hooks` (`AsyncLocalStorage`)
- `buffer` (`Buffer`, also a global; see [Buffer](#buffer))
- `sqlite`
- timers, and more

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
| functions | `pipeline(...streams, cb)`, `finished(stream, [options], cb)`, `isReadable`, `isWritable`, `isErrored`, `isDisturbed`, `getDefaultHighWaterMark`/`setDefaultHighWaterMark` (64 KiB, 16 objects) |
| `node:stream/promises` | `pipeline(...streams)`, `finished(stream, options?)` |
| `node:fs` | `createReadStream(path, { start, end, highWaterMark, encoding, fd, flags, autoClose })` and `createWriteStream(path, { flags, encoding, start, fd })`, `ReadStream`/`WriteStream` (`'open'`, `'ready'`, `bytesRead`/`bytesWritten`, `close()`); errors as Node's (`ENOENT: no such file or directory, open '...'`, `code`, `syscall`) |

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
- `fs.createReadStream` and `createWriteStream` must be imported by name (`import { createReadStream } from
  "node:fs"`), not through `import fs from "node:fs"`.

### stream/web

`node:stream/web` has `ReadableStream` (with `getReader()`/`read()`/`releaseLock()`, `cancel`, `pipeTo`,
`pipeThrough`, `tee`, `for await`), `WritableStream` (`getWriter()` with `write`/`close`/`abort`/`ready`/
`closed`/`desiredSize`), `TransformStream`, `TextEncoderStream`, `TextDecoderStream`,
`CountQueuingStrategy` and `ByteLengthQueuingStrategy`. They are not globals: import them. A source, sink or
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

## Default imports

`import fs from "node:fs"`, `import path from "path"` and the like (TypeScript's `esModuleInterop`) read as
`import * as fs from "node:fs"`: the default export of a Node module is the module itself. `node:events` is the
exception, as in Node: its default export is `EventEmitter`.

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

- **Request:** `method`, `url` (path and query), `headers` (lower-case names), `rawHeaders`, `httpVersion`. The
  request (and the client's response) is a `stream.Readable`: `'data'`/`'end'`, `setEncoding`, `pipe()` and
  `for await (const chunk of req)`.
- **Response:** `statusCode`, `setHeader`/`appendHeader`/`getHeader`/`getHeaders`/`hasHeader`/`removeHeader`,
  `writeHead(status, headers)`, `write`, `end`, and `'finish'`.
- **Server:** `listen(port, cb)`, with port 0 choosing a free port, `address()`, `close(cb)`, and the
  `'request'`, `'listening'` and `'close'` events.

`listen()` returns at once, as in Node. The server answers once the program's own code has run, on the same event
loop as its timers and other sockets. `close()` lets the program end normally.

The client side, `http.request(url | options, cb)` and `http.get(...)`, runs on `fetch`. You get a
`ClientRequest` (`setHeader`, `write`, `end`, `'response'`, `'error'`) and an `IncomingMessage` response
(`statusCode`, `statusMessage`, `headers`, `'data'`/`'end'`). `STATUS_CODES` and `METHODS` are there too.

Differences from Node:

- Bodies are text: binary request and response bodies are read and written as UTF-8.
- A response is sent when `end()` is called. `write()` buffers, so there is no chunked streaming yet. For
  streaming, use xisto's `HttpResponse.stream`.
- One server per program.
- The server class is `HttpServer`. The name `Server` is xisto's built-in server, so `import { Server } from
  "node:http"` does not work; `http.createServer` does.
- `https.createServer` and `Agent` are not there yet.

## net

`net.createServer((socket) => ...)` and `net.createConnection(port, host, cb)` / `net.connect` give TCP servers
and sockets:

- **Socket:** `'connect'`, `'data'` (Buffers, or strings after `setEncoding`), `'end'`, `'close'` (with `hadError`)
  and `'error'` (with Node's `code`: `ECONNREFUSED`, `ENOTFOUND`...). Methods `write(data, cb)`, `end(data)` and
  `destroy()`. Fields `remoteAddress`/`remotePort`/`localAddress`/`localPort`, `bytesRead`/`bytesWritten`.
- **Server:** `listen(port, host, cb)` (port 0 picks a free one), `address()`, `close(cb)` (waits for the open
  connections), `getConnections`.
- `isIP`, `isIPv4`, `isIPv6`.

Each socket reads on a task of its own, so `'data'` arrives as the bytes do.

Differences from Node:

- IPv4 listeners only. No Unix domain sockets.
- No read backpressure (`pause()`).
- `write()` writes before it returns.
- The socket is an `EventEmitter`, not a `stream.Duplex` (no `pipe()`).
- The server class is `NetServer`, because `Server` is xisto's built-in.

## dns

`dns.lookup(name, cb)`, `dns.resolve4` / `resolve6`, and `dns.promises.lookup(name, { family })` (plus
`lookupAll`), through the system resolver as Node's `lookup` does. A name that does not exist gives `ENOTFOUND`.

Differences from Node: `resolve4`/`resolve6` also use the system resolver (Node asks a DNS server). The record
queries (`resolveMx`, `resolveTxt`, `reverse`...) are not there yet. `promises.lookup(name, { all: true })` is
`promises.lookupAll(name)`.

## child_process

| API | Notes |
|---|---|
| `spawn(file, args, { cwd, env, shell })` | `stdout`/`stderr` give `'data'` as the child writes (`setEncoding` for text); `stdin.write`/`end`; `kill(signal)`; `'spawn'`, `'exit'` (code, signal), `'close'`, `'error'` (`spawn x ENOENT`) |
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

## console.log and inspect

Strings inside objects are quoted as Node quotes them: `'...'`, or `"..."` when the string has a `'`, or `` `...` ``
when it has both; `\`, tabs, `\r` and other control characters are escaped (`\x1B`). A long string with line
breaks inside an object is printed one line per piece, joined by `+`, as Node does.

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
