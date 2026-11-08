// A loop that touches a Map<int, int> only through the counting idiom holds the table's fields in locals, read
// again after the slow path (an insert that grows the table, a value or key past 32 bits that widens it); the byte
// arrays it reads get their data pointer in a local; and x % 2^n of a value that is never negative is a mask.
function counts(bytes: Uint8Array, k: int): Map<int, int> {
  const m = new Map<int, int>();
  let key: int = 0;
  for (let i = 0; i < k; i++) key = key * 4 + (bytes[i] & 3);
  const top: int = k === 1 ? 1 : k === 2 ? 4 : 16;
  for (let j = k; j < bytes.length; j++) {
    key = (key % top) * 4 + (bytes[j] & 3);
    if (m.has(key)) m.set(key, m.get(key)! + 1);
    else m.set(key, 1);
  }
  return m;
}
function show(m: Map<int, int>): string {
  const ks = Array.from(m.keys()).sort((a: int, b: int) => a - b);
  let s = "";
  for (const k of ks) s += `${k}:${m.get(k)!} `;
  return s;
}
const b = new Uint8Array(5000);
let x: int = 12345;
for (let i = 0; i < b.length; i++) { x = (x * 75 + 74) % 65537; b[i] = x % 256; }
console.log(show(counts(b, 1)));
console.log(show(counts(b, 3)).length, counts(b, 3).size);

// growth in the middle: thousands of distinct keys from an empty table
const g = new Map<int, int>();
for (let i = 0; i < 20000; i++) {
  const k = (i * 7919) % 30011;
  if (g.has(k)) g.set(k, g.get(k)! + 2);
  else g.set(k, 2);
}
let sum: int = 0;
for (const [, v] of g) sum += v;
console.log(g.size, sum);

// widening in the middle: keys and sums past 32 bits
const w = new Map<int, int>();
const big: int = 3000000000;
for (let i = 0; i < 100; i++) {
  const k = i < 50 ? i % 7 : big + (i % 3);
  const d: int = i === 60 ? 2147483647 : 1;
  if (w.has(k)) w.set(k, w.get(k)! + d);
  else w.set(k, d);
}
console.log(show(w));

// a negative value: the remainder keeps its sign
let neg: int = -5;
const out: int[] = [];
for (let i = 0; i < 6; i++) { neg = neg % 4; out.push(neg); neg = neg * 3 - 1; }
console.log(out.join(","));

// the map read outside the idiom in the loop: not held, same answers
const r = new Map<int, int>();
let seen: int = 0;
for (let i = 0; i < 50; i++) {
  const k = i % 5;
  if (r.has(k)) r.set(k, r.get(k)! + 1);
  else r.set(k, 1);
  seen += r.size;
}
console.log(show(r), seen);
