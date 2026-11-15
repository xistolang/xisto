// performance's User Timing: mark (with detail and startTime), measure (marks, options, durations), getEntries,
// getEntriesByName/Type, clearMarks/clearMeasures, timeOrigin; through node:perf_hooks too (the same object).
// Times vary from run to run: only their relations are printed
import { performance as perf, PerformanceMark, PerformanceMeasure, PerformanceEntry } from "node:perf_hooks";

const t0 = performance.now();
const a = performance.mark("a", { detail: { step: 1 } });
console.log(a.name, a.entryType, a.duration, a.detail, a instanceof PerformanceMark, a instanceof PerformanceEntry);
console.log(a.startTime >= t0, typeof performance.timeOrigin, performance.timeOrigin > 1600000000000);
let x = 0;
for (let i = 0; i < 100000; i++) x += i;
const b = perf.mark("b");
console.log(b.detail, b.startTime >= a.startTime, x);
const m = performance.measure("a to b", "a", "b");
console.log(m.name, m.entryType, m.duration === b.startTime - a.startTime, m.startTime === a.startTime, m instanceof PerformanceMeasure);
const toNow = performance.measure("a to now", "a");
console.log(toNow.duration >= 0, toNow.detail);
console.log(performance.getEntries().map((e) => `${e.name}:${e.entryType}`));
// (fixed times: Node's clock starts earlier than xisto's, so these sort differently on the timeline; printed sorted)
const fixed = performance.measure("fixed", { start: 10, end: 25, detail: "d" });
console.log(fixed.startTime, fixed.duration, fixed.detail);
const dur = performance.measure("dur", { start: 5, duration: 2 });
console.log(dur.startTime, dur.duration);
const at = performance.mark("at", { startTime: 3 });
console.log(JSON.stringify(at));
console.log(performance.getEntriesByType("mark").map((e) => e.name).sort());
console.log(performance.getEntriesByType("measure").map((e) => e.name).sort());
console.log(performance.getEntriesByName("a").length, performance.getEntriesByName("a", "measure").length);
performance.clearMarks("a");
console.log(performance.getEntriesByType("mark").map((e) => e.name).sort());
performance.clearMeasures("fixed");
console.log(performance.getEntriesByType("measure").map((e) => e.name).sort());
performance.clearMarks();
performance.clearMeasures();
console.log(performance.getEntries().length);
try {
  performance.measure("bad", "never-set");
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
console.log(perf.now() >= t0);
