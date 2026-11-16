// a generic function that takes Record<string, T>, called with an object literal: T from the literal's values, as
// TypeScript infers it (it was XS3010 "cannot infer type parameter T"); a null among them makes it T | null
interface Box { w: number }
function vals<T>(o: Record<string, T>): T[] { return Object.values(o); }
function keysOf<T>(o: Record<string, T>, pick: (v: T) => boolean): string[] { return Object.keys(o).filter((k) => pick(o[k])); }
const n = 3;
console.log(vals({ a: n, b: n * 2 }).join(","));
console.log(vals({ x: { w: 1 }, y: { w: 2 } }).map((b) => b.w).join(","));
const b1: Box = { w: 5 };
console.log(vals({ only: b1 })[0].w);
console.log(keysOf({ a: 1, b: 5, c: 9 }, (v) => v > 2).join(","));
console.log(vals({ s: "p", t: `q${n}` }).join(""));
console.log(vals({ a: 1, b: null }).length);
console.log(vals({ a: "s", b: null }).length, vals<number | null>({ a: 1, b: null }).length);
