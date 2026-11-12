// dd-trace for xisto: the tracer's API with tracing off. A program built with xisto keeps its init() call and
// starts; no spans are sent (APM in a native binary needs its own exporter, not Node's instrumentation).
// Compatibility: init() writes one line on stderr saying tracing is off; every other call does nothing.

/** The options of tracer.init() that programs pass (all are accepted and ignored). */
export interface TracerOptions {
  service?: string;
  env?: string;
  version?: string;
  logInjection?: boolean;
  runtimeMetrics?: boolean;
  profiling?: boolean;
  sampleRate?: number;
}

let started = false;

/** tracer.init(options): tracing stays off in xisto; says so once. */
export function init(options: TracerOptions = {}): void {
  if (started) {
    return;
  }
  started = true;
  const service = options.service ?? "service";
  console.error(`dd-trace: tracing is off in xisto (${service}); init() accepted, no spans are sent`);
}

/** tracer.use(plugin, options): instrumentation is off; accepted. */
export function use(plugin: string, options: unknown = null): void {}

/** A span's ids (tracing is off: zeros, as dd-trace's no-op span gives). */
export class SpanContext {
  toTraceId(): string {
    return "0";
  }
  toSpanId(): string {
    return "0";
  }
}

/** A span: tags, finish and context accepted and kept nowhere (tracing is off in xisto, as dd-trace's no-op span). */
export class Span {
  setTag(key: string, value: unknown): Span {
    return this;
  }
  addTags(tags: unknown): Span {
    return this;
  }
  finish(finishTime: number | null = null): void {}
  context(): SpanContext {
    return new SpanContext();
  }
}

/** The active span (tracing is off in xisto: none). */
export class Scope {
  /** the span of the current request: null (no spans are made) */
  active(): Span | null {
    return null;
  }
}

/** tracer.startSpan(name, options): a span that records nothing (tracing is off). */
export function startSpan(name: string, options: unknown = null): Span {
  return new Span();
}

/** tracer.trace(name, options, fn): fn run with a span that records nothing; its value (a promise too) returned as is. */
export function trace<T>(name: string, options: unknown, fn: (span: Span) => T): T {
  return fn(new Span());
}

/** tracer.setUser(user): accepted (tracing is off). */
export function setUser(user: unknown): void {}

/** tracer.scope(): the scope manager (its active() is always null here). */
export function scope(): Scope {
  return new Scope();
}
