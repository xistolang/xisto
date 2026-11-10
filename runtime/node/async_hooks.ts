// xisto:async_hooks (node:async_hooks): AsyncLocalStorage and AsyncResource.
// A context is a chain of frames, one per run()/enterWith(); the runtime keeps the current one per coroutine
// (xs_als_cur): an async call, an await, a Promise.all task and a timer keep the context of the code that started them,
// as in Node. getStore() gives null where Node gives undefined (xisto has no undefined).

class AlsFrame {
  parent: AlsFrame | null;
  key: int;
  value: unknown;
  has: boolean;
  constructor(parent: AlsFrame | null, key: int, value: unknown, has: boolean) {
    this.parent = parent;
    this.key = key;
    this.value = value;
    this.has = has;
  }
}

declare function als_current(): AlsFrame | null;
declare function als_set(frame: AlsFrame | null): void;

let nextKey: int = 1;

/** A value that follows the asynchronous flow: what run() stores is what getStore() reads in every call, await and
 * timer started inside it, and nowhere else. */
export class AsyncLocalStorage<T> {
  private key: int;
  constructor() {
    this.key = nextKey;
    nextKey++;
  }
  /** The store of the innermost run()/enterWith() of this storage around the current code; null outside any. */
  getStore(): T | null {
    let f = als_current();
    while (f !== null) {
      if (f.key === this.key) {
        if (!f.has) return null;
        return f.value as T;
      }
      f = f.parent;
    }
    return null;
  }
  /** Runs fn with store as this storage's value; the previous context comes back when fn returns or throws. */
  run<R>(store: T, fn: () => R): R {
    const prev = als_current();
    als_set(new AlsFrame(prev, this.key, store, true));
    try {
      return fn();
    } finally {
      als_set(prev);
    }
  }
  /** Runs fn with no store for this storage. */
  exit<R>(fn: () => R): R {
    const prev = als_current();
    als_set(new AlsFrame(prev, this.key, null, false));
    try {
      return fn();
    } finally {
      als_set(prev);
    }
  }
  /** Sets the store for the rest of the current execution (and what it starts). */
  enterWith(store: T): void {
    als_set(new AlsFrame(als_current(), this.key, store, true));
  }
  /** No store from here on, until a new run()/enterWith(). */
  disable(): void {
    als_set(new AlsFrame(als_current(), this.key, null, false));
  }
}

/** The context of the code that created it, to run later code in: a callback registered now runs, later, with the
 * stores it would have seen now. */
export class AsyncResource {
  readonly type: string;
  private frame: AlsFrame | null;
  constructor(type: string) {
    this.type = type;
    this.frame = als_current();
  }
  /** Runs fn in this resource's context. */
  runInAsyncScope<R>(fn: () => R): R {
    const prev = als_current();
    als_set(this.frame);
    try {
      return fn();
    } finally {
      als_set(prev);
    }
  }
  /** fn, bound to this resource's context. */
  bind(fn: () => void): () => void {
    return (): void => {
      this.runInAsyncScope<void>(fn);
    };
  }
  emitDestroy(): AsyncResource {
    return this;
  }
  /** fn, bound to the current context. */
  static bind(fn: () => void): () => void {
    const r = new AsyncResource("bound-anonymous-fn");
    return r.bind(fn);
  }
}
