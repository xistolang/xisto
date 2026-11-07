// Generators used as values, compiled as state machines (XB-022): the frame keeps the locals between pulls
function* range(from: int, to: int, step: int): Generator<int> {
  for (let i: int = from; i < to; i += step) { yield i; }
}
function* labels(prefix: string, n: int): Generator<string> {
  let k: int = 0;
  while (true) {
    if (k === n) { return; }
    yield `${prefix}${k}`;
    k++;
  }
}
function* pairsUpTo(n: int): Generator<int> {
  for (let a: int = 1; a <= n; a++) {
    for (let b: int = a; b <= n; b++) {
      if ((a + b) % 2 === 1) { continue; }
      yield a * 100 + b;
    }
  }
}
class Counter {
  base: int;
  constructor(b: int) { this.base = b; }
  *upTo(n: int): Generator<int> {
    let x: number = 0.5;
    for (let i: int = 0; i < n; i++) { x = x * 2; yield this.base + i; }
  }
}
function total(g: Generator<int>): int {
  let s: int = 0;
  for (const v of g) { s += v; }
  return s;
}
function firstOver(g: Generator<int>, limit: int): int {
  for (const v of g) { if (v > limit) { return v; } }
  return -1;
}
const r = range(0, 20, 3);
console.log(total(r));
const ls = labels("item-", 4);
const parts: string[] = [];
for (const l of ls) { parts.push(l); }
console.log(parts.join(","));
console.log(total(pairsUpTo(5)), firstOver(pairsUpTo(6), 300));
const c = new Counter(1000);
console.log(total(c.upTo(5)));
const big = range(0, 1000000, 1);
console.log(firstOver(big, 10)); // stops early: the generator is dropped half-way
const words = labels("w", 3);
let joined = "";
for (const w of words) { joined += w; if (w === "w1") { break; } }
console.log(joined);
