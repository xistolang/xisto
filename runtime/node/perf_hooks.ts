// xisto:perf_hooks (node:perf_hooks): the User Timing part of performance, written in xisto. performance.now() stays
// the compiler's own (one clock read); the compiler reads performance.X for the other members as
// XistoPerformance.X (src/checker.c) and loads this module when the program names one (src/parser.c).
// `import { performance } from "node:perf_hooks"` is the global performance.
// Differences from Node (docs/docs/guides/node-compatibility.md): no PerformanceObserver, no resource or event-loop
// timing (eventLoopUtilization, nodeTiming, timerify); toJSON gives { timeOrigin } only; the error for a mark that
// was not set is a SyntaxError (Node: a DOMException named SyntaxError).

/** A mark or a measure of the performance timeline. */
export class PerformanceEntry {
  readonly name: string;
  readonly entryType: string;
  readonly startTime: number;
  readonly duration: number;
  readonly detail: any;
  constructor(name: string, entryType: string, startTime: number, duration: number, detail: any) {
    this.name = name;
    this.entryType = entryType;
    this.startTime = startTime;
    this.duration = duration;
    this.detail = detail;
  }
  toJSON(): PerformanceEntryJSON {
    return { name: this.name, entryType: this.entryType, startTime: this.startTime, duration: this.duration, detail: this.detail };
  }
}

/** What JSON.stringify writes for an entry. */
export interface PerformanceEntryJSON {
  name: string;
  entryType: string;
  startTime: number;
  duration: number;
  detail: any;
}

/** performance.mark(name): a named point in time. */
export class PerformanceMark extends PerformanceEntry {
  constructor(name: string, startTime: number, detail: any) {
    super(name, "mark", startTime, 0, detail);
  }
}

/** performance.measure(name, start, end): the time between two marks. */
export class PerformanceMeasure extends PerformanceEntry {
  constructor(name: string, startTime: number, duration: number, detail: any) {
    super(name, "measure", startTime, duration, detail);
  }
}

/** performance.mark's options. */
export interface PerformanceMarkOptions {
  detail?: any;
  startTime?: number;
}

/** performance.measure's options: start and end as mark names or times, or a duration. */
export interface PerformanceMeasureOptions {
  start?: string | number;
  end?: string | number;
  duration?: number;
  detail?: any;
}

/** performance.toJSON(). */
export interface PerformanceJSON {
  timeOrigin: number;
}

const entries: PerformanceEntry[] = [];
const origin: number = Date.now() - performance.now();

function markTime(name: string): number {
  for (let i = entries.length - 1; i >= 0; i--) {
    const e = entries[i];
    if (e.entryType === "mark" && e.name === name) return e.startTime;
  }
  throw new SyntaxError(`The "${name}" performance mark has not been set`);
}

function timeOf(v: string | number): number {
  if (typeof v === "string") return markTime(v as string);
  return v as number;
}

function add(e: PerformanceEntry): void {
  // (the timeline in startTime order, as getEntries gives it)
  entries.push(e);
  let i = entries.length - 1;
  while (i > 0 && entries[i - 1].startTime > e.startTime) {
    entries[i] = entries[i - 1];
    i--;
  }
  entries[i] = e;
}

function clear(type: string, name: string | null): void {
  for (let i = entries.length - 1; i >= 0; i--) {
    const e = entries[i];
    if (e.entryType === type && (name === null || e.name === name)) entries.splice(i, 1);
  }
}

/** performance's members beyond now() (the compiler reads performance.X as XistoPerformance.X). */
export class XistoPerformance {
  /** The time the process started, in milliseconds since the epoch (what now() counts from). */
  static readonly timeOrigin: number = origin;
  static now(): number {
    return performance.now();
  }
  static mark(name: string, options: PerformanceMarkOptions | null = null): PerformanceMark {
    let start = performance.now();
    let detail: any = null;
    if (options !== null) {
      if (options.startTime !== undefined) start = options.startTime;
      if (options.detail !== undefined) detail = options.detail;
    }
    const m = new PerformanceMark(name, start, detail);
    add(m);
    return m;
  }
  static measure(name: string, startOrOptions: string | PerformanceMeasureOptions | null = null, endMark: string | null = null): PerformanceMeasure {
    let start = 0;
    let end = performance.now();
    let detail: any = null;
    if (typeof startOrOptions === "string") {
      start = markTime(startOrOptions as string);
    } else if (startOrOptions !== null) {
      const o = startOrOptions as PerformanceMeasureOptions;
      if (o.detail !== undefined) detail = o.detail;
      const hasStart = o.start !== undefined;
      const hasEnd = o.end !== undefined;
      if (hasStart) start = timeOf(o.start!);
      if (hasEnd) end = timeOf(o.end!);
      if (o.duration !== undefined) {
        if (hasStart) end = start + o.duration;
        else start = end - o.duration;
      }
    }
    if (endMark !== null) end = markTime(endMark);
    const m = new PerformanceMeasure(name, start, end - start, detail);
    add(m);
    return m;
  }
  static getEntries(): PerformanceEntry[] {
    return entries.slice();
  }
  static getEntriesByName(name: string, type: string | null = null): PerformanceEntry[] {
    return entries.filter((e) => e.name === name && (type === null || e.entryType === type));
  }
  static getEntriesByType(type: string): PerformanceEntry[] {
    return entries.filter((e) => e.entryType === type);
  }
  static clearMarks(name: string | null = null): void {
    clear("mark", name);
  }
  static clearMeasures(name: string | null = null): void {
    clear("measure", name);
  }
  static toJSON(): PerformanceJSON {
    return { timeOrigin: origin };
  }
}
