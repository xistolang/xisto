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
- `sqlite`
- timers, and more

The output of `console.log`, value formatting and error messages follow Node's, and errors carry Node-style stack
traces.

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
