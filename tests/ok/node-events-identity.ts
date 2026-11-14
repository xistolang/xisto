// off()/removeListener() with the same function value however it is read: this.onData, obj.h, and a variable
// reassigned after on() (the listener stays: it was another function)
import { EventEmitter } from "node:events";
class C {
  em = new EventEmitter();
  n = 0;
  onData = (x: number): void => { this.n += x; console.log("data", x); };
  start(): void { this.em.on("d", this.onData); }
  stop(): void { this.em.off("d", this.onData); }
}
const c = new C();
c.start();
c.em.emit("d", 1);
c.stop();
console.log(c.em.listenerCount("d"));
c.em.emit("d", 2);
const holder = { h: (x: number): void => console.log("holder", x) };
c.em.on("e", holder.h);
c.em.removeListener("e", holder.h);
console.log(c.em.listenerCount("e"));
function make(): (x: number) => void {
  let f = (x: number): void => console.log("f", x);
  c.em.on("g", f);
  f = (x: number): void => console.log("f2", x);
  return f;
}
const f2 = make();
c.em.off("g", f2);
console.log(c.em.listenerCount("g"));
let top = (x: number): void => console.log("top1", x);
c.em.on("t", top);
top = (x: number): void => console.log("top2", x);
c.em.off("t", top);
console.log(c.em.listenerCount("t"));
