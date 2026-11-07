// XB-034: `let i = 0` is an int when every assignment to it is an integer by its form, so the TypeScript loop
// `for (let i = 0; i < a.length; i++) a[i]` compiles; read where a number goes, it becomes one by itself.
// Any other assignment keeps it a number, as before. The output is Node's.
const a: number[] = [1.5, 2, 3];
let s = 0;
for (let i = 0; i < a.length; i++) { s += a[i]; }
const w = "abc";
const codes: number[] = [];
for (let j = 0; j < w.length; j++) { codes.push(w.charCodeAt(j)); }
let count = 0;
for (const x of a) { if (x > 1) { count++; } }
console.log(s, codes, count, count / 2, Math.sqrt(count), count * 0.5, count + 0.25);
function twice(n: number): number { return n * 2; }
let k = 0;
k += 3;
const mixed: number[] = [k, 1.5];
console.log(twice(k), mixed, k < 3.5, k === 3);
let idx = 0;
while (idx < a.length) { idx = idx + 1; }
let m = 10;
m = Math.floor(m / 3);
console.log(idx, m, `${m}`, -m);
// these stay numbers
let half = 0;
half += 0.5;
let scaled = 1;
scaled *= 1.5;
let divided = 8;
divided /= 3;
function seven(): number { return 7; }
let fromCall = 0;
fromCall = seven();
let global = 0;
function bump(): void { global += 0.25; }
bump();
bump();
console.log(half, scaled, divided, fromCall, global);
let fact = 1;
for (let i = 1; i <= 25; i++) { fact *= i; } // a product stays a number: 25! is past 2^63
let big = 1;
for (let i = 0; i < 70; i++) { big = big * 2; }
console.log(fact, big);
class Vec2 {
  private _x: number = 3;
  get length(): number { return this._x / 2; } // a getter named length that gives a number
}
const vv = new Vec2();
let acc = 0;
for (let i = 0; i < 3; i++) { acc += vv.length; }
console.log(acc);
class Rect { w: number; h: number; constructor(w: number, h: number) { this.w = w; this.h = h; } }
const rs: Rect[] = [];
for (let i = 0; i < 4; i++) { rs.push(new Rect(i % 3, -i + 1)); }
let areas = "";
for (const r of rs) { areas += `${r.w * r.h + 0.5} `; } // (a loop, not map: the qbe backend checks this file too)
console.log(areas);
// typed names, int[] elements and untyped consts count by their form (fannkuch-redux): every index below needs an int
function rotate(n: int, perm: int[]): int {
  let r = 0, k = 0, i = 0;
  r = n - 1;
  k = perm[0];
  while (i < r) { const j = i + 1; perm[i] = perm[j]; i = j; }
  perm[r] = k;
  return perm[k];
}
console.log(rotate(4, [3, 1, 2, 0]));
const halfc = 0.5;
let q = 0;
q = halfc; // a const with a number: q stays a number
console.log(q + 1);
