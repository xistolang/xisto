// o.m read as a value is the one function of (o, m), as in JavaScript: o.m === o.m, and emitter.off(e, o.m)
// removes the listener emitter.on(e, o.m) added (the methods here do not read this: called unbound in Node)
import { EventEmitter } from "node:events";
class Handler {
  id: number;
  constructor(id: number) {
    this.id = id;
  }
  onTick(v: number): void {
    console.log("tick", v);
  }
  twice(x: number): number {
    return x * 2;
  }
}
const em = new EventEmitter();
const a = new Handler(1);
const b = new Handler(2);
const f = a.twice;
const g = a.twice;
console.log(f === g, a.twice === a.twice, a.twice === b.twice, f !== g);
em.on("tick", a.onTick);
em.on("tick", b.onTick);
em.emit("tick", 1);
em.off("tick", a.onTick);
console.log(em.listenerCount("tick"));
em.emit("tick", 2);
em.removeListener("tick", b.onTick);
console.log(em.listenerCount("tick"));
const fs: Array<(x: number) => number> = [a.twice, b.twice];
console.log(fs.indexOf(b.twice), fs.includes(a.twice), fs.indexOf(f));
console.log(a.twice, [a.onTick]);
const bound = a.twice.bind(a);
console.log(bound === a.twice, a.twice.bind(a) === bound, bound(4));
for (let i = 0; i < 3; i++) {
  const h = new Handler(i);
  em.once("tick", h.onTick);
}
console.log(em.listenerCount("tick"));
em.emit("tick", 3);
console.log(em.listenerCount("tick"));
class Wired {
  em: EventEmitter;
  constructor(em: EventEmitter) {
    this.em = em;
  }
  start(): void {
    this.em.on("tick", this.log);
  }
  stop(): void {
    this.em.off("tick", this.log);
  }
  log(v: number): void {
    console.log("wired", v);
  }
}
const w = new Wired(em);
w.start();
em.emit("tick", 4);
w.stop();
em.emit("tick", 5);
console.log(em.listenerCount("tick"), w.log === new Wired(em).log);
