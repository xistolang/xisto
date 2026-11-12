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

/** The active span (tracing is off in xisto: none). */
export class Scope {
  /** the span of the current request: null (no spans are made) */
  active(): unknown {
    return null;
  }
}

/** tracer.scope(): the scope manager (its active() is always null here). */
export function scope(): Scope {
  return new Scope();
}
