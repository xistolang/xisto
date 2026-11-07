// Makes Xisto sources type-check under tsc. Node and Bun ignore type annotations.
type int = number;

// Server side (Xisto only; Node and Bun have their own server APIs).
declare class HttpRequest {
  readonly method: string;
  readonly path: string;
  readonly body: string;
  query(name: string): string;
  header(name: string): string;
}
declare class HttpResponse {
  readonly status: int;
  readonly body: string;
  static text(body: string): HttpResponse;
  static json(body: string): HttpResponse;
  static status(code: int, body: string): HttpResponse;
}
declare class Server {
  get(path: string, handler: (req: HttpRequest) => HttpResponse): void;
  post(path: string, handler: (req: HttpRequest) => HttpResponse): void;
  listen(port: int): void;
  /** HTTPS: PEM certificate chain and private key (RSA or EC), as file paths. */
  listen(port: int, tls: { cert: string; key: string }): void;
}

// Divergences that need a declaration for tsc: process.env is a Map in Xisto.
declare namespace process {
  const env: Map<string, string>;
}

// Streams (Xisto only): lazy, pull-based pipelines over files.
declare class Stream<T> {
  map<U>(fn: (x: T) => U): Stream<U>;
  filter(fn: (x: T) => boolean): Stream<T>;
  batch(n: int): Stream<T[]>;
  take(n: int): Stream<T>;
  forEach(fn: (x: T) => void): void;
  reduce<U>(fn: (acc: U, x: T) => U, init: U): U;
  toArray(): T[];
  count(): int;
  writeLines(path: string): void;
  [Symbol.asyncIterator](): AsyncIterator<T>;
}
declare function lines(path: string): Stream<string>;
declare function chunks(path: string, bytes: int): Stream<string>;
