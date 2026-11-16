// events.on (async iteration of emits), EventEmitter's statics (listenerCount, once, on...) and stream.destroy
import { EventEmitter, on, once } from "node:events";
import * as events from "node:events";
import { Readable, PassThrough, destroy, isDestroyed } from "node:stream";
import * as stream from "node:stream";

async function main(): Promise<void> {
  const ee = new EventEmitter();
  ee.on("x", () => {});
  ee.on("x", () => {});
  console.log(EventEmitter.listenerCount(ee, "x"), EventEmitter.listenerCount(ee, "y"), events.listenerCount(ee, "x"));
  console.log(EventEmitter.getEventListeners(ee, "x").length, EventEmitter.getMaxListeners(ee));
  EventEmitter.setMaxListeners(3, ee);
  console.log(ee.getMaxListeners());

  // emits before the loop starts are queued; the iteration ends with a close event
  const it = on(ee, "foo", { close: ["end"] });
  ee.emit("foo", "early", 1);
  process.nextTick(() => {
    ee.emit("foo", "bar");
    ee.emit("foo", 42, true);
    ee.emit("foo");
    ee.emit("end");
    ee.emit("foo", "after end");
  });
  for await (const args of it) console.log("got", args);
  console.log("loop done", ee.listenerCount("foo"), ee.listenerCount("error"), ee.listenerCount("end"));

  // an 'error' emit ends it: the loop throws it
  const ee2 = new EventEmitter();
  setTimeout(() => {
    ee2.emit("data", 1);
    ee2.emit("error", new Error("kaboom"));
  }, 1);
  try {
    for await (const [v] of EventEmitter.on(ee2, "data")) console.log("data", v);
  } catch (e) {
    console.log("caught", (e as Error).message, ee2.listenerCount("data"));
  }

  // the static once
  setTimeout(() => ee2.emit("ready", "a", "b"), 1);
  console.log(await EventEmitter.once(ee2, "ready"), await Promise.resolve(1));
  setTimeout(() => ee2.emit("ready2", 7), 1);
  console.log(await once(ee2, "ready2"));

  // stream.destroy: with an error, and without one on a stream that is not done (an AbortError)
  const r = new Readable({ read(): void {} });
  r.on("error", (e: Error) => console.log("r error", e.message));
  r.on("close", () => console.log("r close", isDestroyed(r)));
  console.log(isDestroyed(r));
  destroy(r, new Error("boom"));
  const p = new PassThrough();
  p.on("error", (e: Error) => console.log("p error", e.name, e.message, (e as any).code));
  p.on("close", () => console.log("p close"));
  stream.destroy(p);
  destroy(p, new Error("again"));
  const done = Readable.from(["a"]);
  done.resume();
  done.on("end", () => {
    done.on("close", () => console.log("done close"));
    done.on("error", (e: Error) => console.log("done error", e.message));
    destroy(done);
  });
}
main();
