// node:timers imported by name (the built-in timers)
import { setTimeout as delay, setImmediate, clearTimeout, setInterval, clearInterval } from "node:timers";
import { setTimeout } from "timers";

setImmediate(() => console.log("immediate"));
const id = setTimeout(() => console.log("cleared: never"), 5);
clearTimeout(id);
delay(() => console.log("aliased setTimeout"), 2);
let n = 0;
const iv = setInterval(() => {
  n++;
  console.log("interval", n);
  if (n === 3) clearInterval(iv);
}, 3);
