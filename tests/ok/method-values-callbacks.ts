// Bound methods: kept in a field and given to an EventEmitter from the constructor (a cycle through the object),
// rest parameters (partial arguments into the rest), async methods, setTimeout, a generic class, an interface;
// xs.map(this.fmt, this) and forEach with the object as thisArg
import { EventEmitter } from "node:events";
class W {
  em: EventEmitter;
  h: (s: string) => void;
  n: number;
  constructor() {
    this.n = 0;
    this.em = new EventEmitter();
    this.h = this.onX.bind(this);
    this.em.on("x", this.onX.bind(this));
  }
  onX(s: string): void { this.n += s.length; }
  sum(...xs: number[]): number { let t = this.n; for (const x of xs) t += x; return t; }
  async later(k: number): Promise<number> { await new Promise<void>((r) => setTimeout(r, 1)); return k + this.n; }
  tick(): void { console.log("tick", this.n); }
}
class Box<T> {
  v: T;
  constructor(v: T) { this.v = v; }
  get(): T { return this.v; }
}
interface Shape { area(): number; }
class Sq implements Shape { s: number; constructor(s: number) { this.s = s; } area(): number { return this.s * this.s; } }
async function main(): Promise<void> {
  const w = new W();
  w.em.emit("x", "abc");
  w.h("zz");
  console.log(w.n);
  const sm = w.sum.bind(w);
  console.log(sm(1, 2, 3), w.sum.bind(w, 10)(1));
  const lt = w.later.bind(w);
  console.log(await lt(1));
  setTimeout(w.tick.bind(w), 0);
  const b = new Box<string>("boxed");
  const g = b.get.bind(b);
  console.log(g());
  const sh: Shape = new Sq(3);
  const ar = sh.area.bind(sh);
  console.log(ar());
}
class F {
  p: string;
  constructor(p: string) { this.p = p; }
  fmt(n: number): string { return this.p + n; }
  run(): string[] { return [1, 2].map(this.fmt, this); }
  each(): void { [3, 4].forEach(this.show, this); }
  show(n: number): void { console.log(this.p, n); }
}
const fo = new F("v");
console.log(fo.run().join(","));
fo.each();
const fm = fo.fmt.bind(fo);
console.log(typeof fm, fo.fmt.name);
main();
