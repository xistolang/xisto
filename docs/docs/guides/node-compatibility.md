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
- `crypto` (`createHash`, `createHmac`, `randomUUID`)
- `stream` (synchronous, no backpressure wait)
- `async_hooks` (`AsyncLocalStorage`)
- `buffer` (`Buffer`, also a global; see [Buffer](#buffer))
- `sqlite`
- timers, and more

The output of `console.log`, value formatting and error messages follow Node's, and errors carry Node-style stack
traces.

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
