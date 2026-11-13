---
title: A NestJS app
---

# A NestJS app

A web server with [NestJS](https://nestjs.com), written exactly as on Node, compiled into a native executable. Each
block below can be copied into a terminal as it is.

## 1. Install xisto, start a project, add NestJS

```sh
brew install xistolang/tap/xisto
xisto init hello-nest && cd hello-nest     # xisto.json, src/main.ts, .gitignore
xisto run                                  # Hello, world!
xisto add @nestjs/common @nestjs/core @nestjs/platform-express
```

`xisto init` creates `xisto.json`, `src/main.ts` (a hello world) and `.gitignore`. `xisto run` compiles and runs
the project's entry and prints `Hello, world!`.

`xisto add` fetches the packages from [xistolang/packages](https://github.com/xistolang/packages) and their
dependencies with them (here `express`, `rxjs`, `class-validator` and `class-transformer`). It does not touch your
code: `src/main.ts` is still the hello world from `xisto init`.

## 2. Write the app

Replace `src/main.ts` with the app, written as on Node:

```sh
cat > src/main.ts <<'EOF'
import { Controller, Get, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';

@Controller()
class AppController {
  @Get()
  hello(): string {
    return 'Hello from xisto';
  }
}

@Module({ controllers: [AppController] })
class AppModule {}

async function bootstrap(): Promise<void> {
  const app = await NestFactory.create(AppModule);
  await app.listen(3123);
}
bootstrap();
EOF
```

## 3. Run it while developing

Run it in one of two ways. While developing, `xisto run` compiles to a temporary file and starts it, with no build
step (the first compile of a Nest app takes a few seconds; wait for `Nest application successfully started`):

```sh
xisto run                        # the server, on port 3123; Ctrl+C stops it
```

For the server, `xisto build` makes an optimised executable (`-O3`, portable) that runs on its own, with nothing installed.
It only compiles; the executable is what you start:

```sh
xisto build                      # ./hello-nest, a native executable of about 1 MB
./hello-nest                     # the server, on port 3123; Ctrl+C stops it
```

Either way, from another terminal:

```sh
curl localhost:3123              # Hello from xisto
```

A fresh checkout restores the packages from `xisto.lock`:

```sh
rm -rf .xisto && xisto install   # as after a git clone
xisto build
```

`xisto add` copies each package into `.xisto/pkgs/` and pins it in `xisto.lock` (source, folder, commit);
`xisto install` restores exactly that, like `npm ci`. The last two lines show what a fresh checkout does: `.xisto/`
is in `.gitignore`, so the packages come back from `xisto.lock`.

## Next

- [Projects and packages](../guides/projects-and-packages.md): `xisto add`, `xisto.lock`, other sources.
- [The language](../language/index.md): the rules xisto adds to TypeScript.
- The NestJS packages available (and the exact subset each one implements) are listed in
  [xistolang/packages](https://github.com/xistolang/packages); the header comment of each package's `index.ts` says
  what it implements and what it does not.
