// Stack allocation (src/stackalloc.c): objects that cannot outlive their function live in its frame; every way
// out must keep them on the heap. The output is Node's.
class P {
  x: number;
  y: number;
  tag: string;
  constructor(x: number, y: number, tag: string) {
    this.x = x;
    this.y = y;
    this.tag = tag;
  }
  len(): number { return Math.sqrt(this.x * this.x + this.y * this.y); }
  self(): P { return this; }
  sum(o: P): number { return this.x + o.x + this.y + o.y; }
  get size(): number { return this.x * this.y; }
}

const kept: P[] = [];
let last: P | null = null;
function store(p: P): void { kept.push(p); }
function pass(p: P): void { store(p); }
function give(p: P): P { return p; }
function keep(p: P): void { last = p; }
function read(p: P): number { return p.x + p.y; }
function deep(p: P, n: number): number { return n === 0 ? read(p) : deep(p, n - 1) + 1; }
class Holder { p: P | null = null; }
class Leaky {
  v: number;
  constructor(v: number) { this.v = v; }
  publish(): void { leaked.push(this); }
}
const leaked: Leaky[] = [];

function escapes(): void {
  for (let i: number = 0; i < 3; i++) {
    const a = new P(i, 1, `a${i}`);
    store(a);                       // into an array
    const b = new P(i, 2, `b${i}`);
    pass(b);                        // through another function
    const c = new P(i, 3, `c${i}`);
    const d = give(c);              // returned back
    kept.push(d);
    const e = new P(i, 4, `e${i}`);
    keep(e);                        // into a global
    const f = new P(i, 5, `f${i}`);
    const h = new Holder();
    h.p = f;                        // into a field
    kept.push(h.p!);
    const g = new P(i, 6, `g${i}`);
    const fn = (): number => g.x;   // captured
    console.log(fn());
    const s = new P(i, 7, `s${i}`);
    kept.push(s.self());            // this returned
    const l = new Leaky(i);
    l.publish();                    // this stored
  }
}
escapes();
console.log(kept.length, kept.map((p: P) => p.tag).join(","), last!.tag, leaked.map((l: Leaky) => l.v).join(","));

function stays(n: number): int {
  let acc = 0;
  for (let i: number = 0; i < n; i++) {
    const p = new P(i, i + 1, `p${i % 10}`);
    const q = new P(1, 2, "q");
    if (i % 5 === 3) { continue; }
    if (p === q) { acc -= 1000; }
    acc += p.len() + p.sum(q) + read(p) + deep(q, 3) + p.size + Number(p.tag.length);
    p.x = 7;                        // a field write is not an escape
    acc += p.x;
    if (i === n - 2) { break; }
  }
  return Math.round(acc);
}
console.log(stays(50));

function tagOf(n: number): string {
  const p = new P(n, n, `t${n}`);
  const t = p.tag;                  // the field's string outlives the object
  return t;
}
console.log(tagOf(4), tagOf(12));

function thrower(p: P): number {
  if (p.x > 2) { throw new Error(`too big: ${p.tag}`); }
  return p.x;
}
function caller(i: number): number {
  const p = new P(i, 0, `e${i}`);
  return thrower(p) + p.y;
}
let total = 0;
for (let i: number = 0; i < 6; i++) {
  try {
    total += caller(i);
  } catch (e) {
    console.log((e as Error).message);
  }
}
console.log(total);
