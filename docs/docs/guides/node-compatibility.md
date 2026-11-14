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
- `crypto`: hashes, HMAC, AES ciphers, pbkdf2/scrypt/hkdf, keys and RSA/ECDSA signatures (see [crypto](#crypto))
- `zlib`: deflate, gzip, raw deflate and Brotli (see [zlib](#zlib))
- `stream` (synchronous, no backpressure wait)
- `async_hooks` (`AsyncLocalStorage`)
- `sqlite`
- timers, and more

The output of `console.log`, value formatting and error messages follow Node's, and errors carry Node-style stack
traces.

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
- missing: Zstd, the stream classes (`createGzip()`...), `params()`,
  `flush()`, the `info` option, and `zlib.promises`-style use without `util.promisify`.

## crypto

`node:crypto` (also `crypto`, `xisto:crypto`). Hashes, HMAC and the key derivations are xisto's own C; ciphers,
RSA and EC are BearSSL's, linked with the module as a static library (a program takes only the functions it
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
  { namedCurve })` (`prime256v1`/`P-256`, `secp384r1`/`P-384`, `secp521r1`/`P-521`); with `publicKeyEncoding` and
  `privateKeyEncoding` the keys come as PEM text, as Node; `generateKeySync("hmac" | "aes", { length })`;
- signatures: `createSign`/`createVerify` (`update`, `sign(key, encoding?)`, `verify(key, signature,
  encoding?)`) and one-shot `sign`/`verify`, with RSA PKCS#1 v1.5, RSA-PSS (`padding:
  constants.RSA_PKCS1_PSS_PADDING`, `saltLength`) and ECDSA (`dsaEncoding: "der"` or `"ieee-p1363"`); keys as
  PEM text, `KeyObject` or `{ key, ... }`;
- `publicEncrypt` (RSA PKCS#1 v1.5 and OAEP) and `constants`;
- `webcrypto` / `subtle`: `digest` (SHA-1/256/384/512), HMAC keys (`importKey` from `raw` or a `jwk` of kty
  `oct`, `exportKey("raw")`), `sign` and `verify`, as `jose`-style code uses them; `webcrypto.getRandomValues` and
  `randomUUID`.

Byte for byte with Node (tested): hashes and HMACs, every cipher with a fixed key and IV (and GCM's tags),
pbkdf2/scrypt/hkdf, RSA PKCS#1 v1.5 signatures, PEM/DER/JWK of a key read and written back, and the error
messages and codes. ECDSA and RSA-PSS signatures are random in both (BearSSL's ECDSA uses RFC 6979 nonces), so
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
- `asymmetricKeyType` of a secret key is `null` (Node: `undefined`);
- missing: Ed25519/Ed448/X25519 keys, DSA and DH, encrypted private keys (`passphrase`), keys from
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
