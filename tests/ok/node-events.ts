// node:events: EventEmitter as Node programs use it (same output as Node)
import EventEmitter, { once, errorMonitor, getEventListeners } from "node:events";
import { EventEmitter as EE2 } from "events";

class Job extends EventEmitter {
  name: string;
  constructor(name: string) {
    super();
    this.name = name;
  }
  run(n: number): void {
    this.emit("start", this.name);
    for (let i = 1; i <= n; i++) this.emit("progress", i, n);
    this.emit("done", this.name, n * 10);
  }
}

const job = new Job("import");
job.on("start", (name: string) => console.log("start", name));
const onProgress = (i: number, n: number): void => console.log(`progress ${i}/${n}`);
job.on("progress", onProgress);
function onDone(name: string, total: number): void {
  console.log("done", name, total);
}
job.once("done", onDone);
job.run(2);
job.run(1); // once: no second "done"
console.log(job.listenerCount("progress"), job.listenerCount("done"), job.eventNames());

// off with the same function value given to on
job.off("progress", onProgress);
console.log(job.listenerCount("progress"), job.emit("progress", 1, 1));

// order: prepend, chaining, return value of emit
const e = new EE2();
const order: string[] = [];
e.on("x", () => order.push("b"))
  .on("x", () => order.push("c"))
  .prependListener("x", () => order.push("a"))
  .prependOnceListener("x", () => order.push("first-once"));
console.log(e.emit("x"), order.join(","));
order.length = 0;
e.emit("x");
console.log(order.join(","), e.emit("nobody"));

// newListener / removeListener events
const w = new EventEmitter();
w.on("newListener", (event: string) => console.log("newListener", event));
w.on("removeListener", (event: string) => console.log("removeListener", event));
const h = (v: number): void => console.log("h", v);
w.on("data", h);
w.emit("data", 7);
w.removeListener("data", h);
console.log(w.listenerCount("data"));

// same listener twice: removeListener takes the last one
const twice = new EventEmitter();
const t = (): void => console.log("t");
twice.on("a", t);
twice.on("a", t);
console.log(twice.listenerCount("a"), twice.listenerCount("a", t));
twice.off("a", t);
console.log(twice.listenerCount("a"));
twice.emit("a");

// listeners() keeps the functions
const ls = new EventEmitter();
ls.on("k", t);
ls.once("k", h);
const got = ls.listeners("k");
console.log(got.length, got[0] === t, got[1] === h, getEventListeners(ls, "k").length);

// errors: thrown when nobody listens; errorMonitor sees them first
const errs = new EventEmitter();
try {
  errs.emit("error", new Error("boom"));
} catch (err) {
  console.log("caught", (err as Error).message);
}
errs.on(errorMonitor, (err: Error) => console.log("monitor", err.message));
errs.on("error", (err: Error) => console.log("handled", err.message));
errs.emit("error", new Error("second"));

// removeAllListeners
const all = new EventEmitter();
all.on("a", t);
all.on("b", t);
all.on("b", t);
all.removeAllListeners("b");
console.log(all.eventNames(), all.listenerCount("b"));
all.removeAllListeners();
console.log(all.eventNames().length);

// max listeners
const lim = new EventEmitter();
console.log(lim.getMaxListeners(), EventEmitter.defaultMaxListeners);
lim.setMaxListeners(1);
console.log(lim.getMaxListeners());

// a listener that removes itself and one added during emit
const self = new EventEmitter();
const once1 = (): void => {
  console.log("self-removing");
  self.off("go", once1);
  self.on("go", () => console.log("added during emit"));
};
self.on("go", once1);
self.emit("go");
self.emit("go");

// events.once: a promise of the arguments
async function main(): Promise<void> {
  const src = new EventEmitter();
  setTimeout(() => {
    src.emit("ready", "db", 3);
  }, 5);
  const args = await once(src, "ready");
  console.log("once resolved", args.length, args[0], args[1]);
  const bad = new EventEmitter();
  setTimeout(() => {
    bad.emit("error", new Error("failed"));
  }, 5);
  try {
    await once(bad, "ready");
  } catch (err) {
    console.log("once rejected", (err as Error).message);
  }
}
main();
