// rxjs: the part a Nest application uses around a request: an Observable of one value (what an interceptor's
// next.handle() gives), pipe with map, tap, catchError, finalize and mergeMap, of, from, throwError and
// lastValueFrom/firstValueFrom. The Observable is cold: it runs when subscribed or awaited, on the current task,
// as Nest drives it for a request. Operators work on Observable<unknown>, the type an interceptor sees.

/**
 * A source of one value, computed when it runs.
 * @example of(1).pipe(map((n) => (n as int) + 1)).subscribe((v) => console.log(v)) // 2
 * Node: rxjs's Observable can emit many values over time; here it emits one value (or an error) and completes,
 * which is what Nest's handlers, interceptors and guards produce.
 */
export class Observable<T> {
  private readonly source: () => T;
  constructor(source: () => T) {
    this.source = source;
  }
  /** Runs the source and gives its value, or throws its error. */
  run(): T {
    return this.source();
  }
  /**
   * Applies the operators in order: o.pipe(a, b) is b(a(o)).
   * Node: up to six operators (xisto has no rest parameters).
   */
  pipe(
    a: OperatorFunction | null = null,
    b: OperatorFunction | null = null,
    c: OperatorFunction | null = null,
    d: OperatorFunction | null = null,
    e: OperatorFunction | null = null,
    f: OperatorFunction | null = null,
  ): Observable<unknown> {
    const src = this.source;
    let o = new Observable<unknown>((): unknown => src() as unknown);
    for (const op of [a, b, c, d, e, f]) {
      if (op !== null) {
        o = op.apply(o);
      }
    }
    return o;
  }
  /**
   * Runs the source and gives its value to next; an error is thrown to the caller.
   * Node: no error or complete callbacks (rxjs's subscribe({ next, error })).
   */
  subscribe(next: (value: T) => void): Subscription {
    const outer = teardown.deferring;
    teardown.deferring = true;
    const from = teardown.fns.length;
    try {
      const v = this.source();
      teardown.deferring = outer;
      next(v);
    } finally {
      teardown.deferring = outer;
      if (!outer) {
        // as rxjs: finalize callbacks run after the subscriber saw the value (or the error)
        const fns = teardown.fns.slice(from);
        teardown.fns = teardown.fns.slice(0, from);
        for (const fn of fns) fn();
      }
    }
    return new Subscription();
  }
}

class Teardown {
  deferring: boolean = false;
  fns: (() => void)[] = [];
}
/** finalize callbacks waiting for subscribe's next to see the value */
const teardown = new Teardown();

/** What subscribe() gives; unsubscribing a completed run does nothing. */
export class Subscription {
  closed: boolean = true;
  unsubscribe(): void {}
}

/**
 * An operator: what pipe() applies, made by map, tap, catchError, finalize or mergeMap.
 * Node: a function in rxjs; here an object with apply(source).
 */
export class OperatorFunction {
  readonly apply: (source: Observable<unknown>) => Observable<unknown>;
  constructor(apply: (source: Observable<unknown>) => Observable<unknown>) {
    this.apply = apply;
  }
}

/** An Observable of the value. */
export function of(value: unknown): Observable<unknown> {
  return new Observable<unknown>((): unknown => value);
}

async function settle(p: Promise<unknown>): Promise<unknown> {
  return await p;
}

/** An Observable of what the promise resolves to (it is awaited when the Observable runs). */
export function from(p: Promise<unknown>): Observable<unknown> {
  return new Observable<unknown>((): unknown => settle(p));
}

/** The value the Observable gives, as a promise. */
export async function lastValueFrom(o: Observable<unknown>): Promise<unknown> {
  return o.run();
}

/** The first value the Observable gives (the only one here), as a promise. */
export async function firstValueFrom(o: Observable<unknown>): Promise<unknown> {
  return o.run();
}

/** An Observable that fails with the error the factory makes. */
export function throwError(factory: () => Error): Observable<unknown> {
  return new Observable<unknown>((): unknown => {
    throw factory();
  });
}

/** Each value through fn. */
export function map(fn: (value: unknown) => unknown): OperatorFunction {
  return new OperatorFunction(
    (source: Observable<unknown>): Observable<unknown> => new Observable<unknown>((): unknown => fn(source.run())),
  );
}

/** Each value through fn, continuing with the Observable fn returns. */
export function mergeMap(fn: (value: unknown) => Observable<unknown>): OperatorFunction {
  return new OperatorFunction(
    (source: Observable<unknown>): Observable<unknown> => new Observable<unknown>((): unknown => fn(source.run()).run()),
  );
}

/**
 * Calls next with each value, passing it on unchanged.
 * Node: no error callback (tap({ next, error })); catchError sees errors.
 */
export function tap(next: (value: unknown) => void): OperatorFunction {
  return new OperatorFunction(
    (source: Observable<unknown>): Observable<unknown> =>
      new Observable<unknown>((): unknown => {
        const v = source.run();
        next(v);
        return v;
      }),
  );
}

/** On an error, continues with the Observable fn returns (or fails with what fn throws). */
export function catchError(fn: (e: Error) => Observable<unknown>): OperatorFunction {
  return new OperatorFunction(
    (source: Observable<unknown>): Observable<unknown> =>
      new Observable<unknown>((): unknown => {
        try {
          return source.run();
        } catch (e) {
          return fn(e).run();
        }
      }),
  );
}

/** Calls fn when the source completes or fails. */
export function finalize(fn: () => void): OperatorFunction {
  return new OperatorFunction(
    (source: Observable<unknown>): Observable<unknown> =>
      new Observable<unknown>((): unknown => {
        const defer = teardown.deferring;
        try {
          return source.run();
        } finally {
          if (defer) teardown.fns.push(fn);
          else fn();
        }
      }),
  );
}
