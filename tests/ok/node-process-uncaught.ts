// process.on('uncaughtException'): an error nothing catches goes to the listeners (with the origin) and the program
// goes on: from a timer (an interval keeps firing) and from an event handler. 'uncaughtExceptionMonitor' sees it
// first. (node-process-uncaught-top: from the top level.)
import { EventEmitter } from "node:events";

const seen: string[] = [];
process.on("uncaughtExceptionMonitor", (err: Error, _origin: string) => {
  seen.push(`monitor ${err.message}`);
});
process.on("uncaughtException", (err: Error, origin: string) => {
  console.log("caught:", err.message, "origin:", origin, err instanceof TypeError ? "TypeError" : "Error");
});
process.on("exit", (code: number) => {
  console.log("monitor saw:", seen.join(", "));
  console.log("exit", code);
});

const bus = new EventEmitter();
bus.on("job", (n: number) => {
  console.log("job", n);
  if (n % 2 === 1) throw new TypeError(`job ${n} failed`);
});

let ticks = 0;
const iv = setInterval(() => {
  ticks++;
  console.log("tick", ticks);
  if (ticks === 3) clearInterval(iv);
  throw new Error(`tick ${ticks}`);
}, 5);

setTimeout(() => {
  bus.emit("job", 1);
  console.log("not reached");
}, 30);
setTimeout(() => {
  bus.emit("job", 2);
  console.log("job 2 done");
}, 40);
setTimeout(() => {
  const words: string[] = ["a", "b"];
  for (const w of words) {
    if (w === "b") throw new Error(`loop at ${w}`);
    console.log("word", w);
  }
}, 50);
setTimeout(() => console.log("last timer"), 60);
