// Object.freeze(x): x itself, read-only through the names it was frozen under (writes are compile errors, see
// tests/err/freeze-*); Object.isFrozen answers as Node for them
interface Pt { x: number; y: number }
const p: Pt = { x: 1, y: 2 };
console.log(Object.isFrozen(p));
const f = Object.freeze(p);
console.log(f.x, f === p, Object.isFrozen(f), Object.isFrozen(p), Object.isFrozen({ a: 1 }), Object.isFrozen(3), Object.isFrozen("s"));
const arr = Object.freeze([1, 2, 3]);
console.log(arr, Object.isFrozen(arr), arr.map((v) => v * 2), arr.slice().reverse());
const CFG = Object.freeze({ a: 1, b: "s", inner: { z: 1 } });
CFG.inner.z = 2; // (shallow, as in Node)
console.log(CFG, JSON.stringify(CFG));
const R: Record<string, number> = Object.freeze({ k: 1 });
console.log(R["k"], Object.isFrozen(R), Object.keys(R));
function show(c: { a: number }): number { return c.a; }
console.log(show(CFG));
let cur = Object.freeze({ n: 1 });
cur = { n: 2 };
cur.n = 3; // (another object: not frozen)
console.log(cur.n);
