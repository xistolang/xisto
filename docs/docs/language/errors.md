---
title: Errors
sidebar_position: 10
---

# Errors

`throw`, `try`, `catch` and `finally` work as in TypeScript. Exceptions cost nothing on the path where nothing is
thrown.

## Throwing and catching

```ts
class NotFound extends Error {
  id: int;
  constructor(id: int) { super(`no item ${id}`); this.id = id; this.name = "NotFound"; }
}
function run(id: int): string {
  try { return load(id); }
  catch (e) {
    if (e instanceof NotFound) return `missing ${e.id}: ${e.message}`;
    throw e;
  } finally { console.log("finally", id); }
}
```

- `catch (e)` gives an `Error`; `e.message` needs no cast. `instanceof` narrows it to a subclass with its fields.
- Rethrowing with `throw e` keeps the original error.
- `new Error(message, { cause })` and `e.cause` work.
- `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError` and `URIError` exist and can be thrown and tested.
- Only `Error` objects and objects of your classes can be thrown:

```text
error[XS8005]: string cannot be thrown
  help: throw new Error("message"), or an object of one of your classes
```

## Run-time errors

Checks that JavaScript does not do are errors in xisto, with a trace: an `int` overflow, an index out of bounds,
a failed `as T` or `x!`, a missing `Map` key for a primitive value. They exit with code 70 when uncaught, and they
can be caught like any `Error`:

```text
runtime error: array index out of bounds
    at get (er3.ts:2:38)
    at er3.ts:3:13
```

## Stack traces

`e.stack` and uncaught errors show the same frames as Node, with file, line and column:

```text
uncaught Error: bottom
    at deep (er1.ts:20:49)
    at deep (er1.ts:20:77)
    at er1.ts:22:1
```

Traces cost nothing until an error happens. Each call site has a static label; on an error the runtime walks the
frame pointers and maps return addresses to those labels. There is no shadow stack and no per-call bookkeeping.
`e.stack` is built only when read.

Limits:

- at most 39 frames are shown;
- frames inside the runtime are not shown (Node shows `at Array.map` between a callback and its caller);
- an uncaught error exits with code 1.

## HTTP handlers

An exception in an HTTP handler answers `500 Internal Server Error` with `{"error":"internal error"}`. The message
is not sent to the client; it is printed on the server with its trace:

```text
xisto: handler failed: secret detail
    at boom (h1.ts:1:55)
```

The server keeps serving other requests.

## See also

- [Functions and async](./functions-async.md) (unhandled rejections)
- [Classes, interfaces and generics](./classes-interfaces-generics.md)
- [Known limitations](./known-limitations.md)
