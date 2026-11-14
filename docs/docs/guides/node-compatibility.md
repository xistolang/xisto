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
- `util`, `querystring`, `string_decoder` (below)
- `http` / `https`: a server and `fetch`, with TLS (AWS-LC by default, or BearSSL)
- `crypto` (`createHash`, `createHmac`, `randomUUID`)
- `stream` (synchronous, no backpressure wait)
- `async_hooks` (`AsyncLocalStorage`)
- `sqlite`
- timers, and more

The output of `console.log`, value formatting and error messages follow Node's, and errors carry Node-style stack
traces.

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
