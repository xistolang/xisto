// Map<int, int> keeps 8-byte slots while every key and value fits in 32 bits, and widens to 16 bytes the
// first time one does not, or before anything but the counting idiom reads it: the answers never change
function show(m: Map<int, int>): string {
  let s = `${m.size}:`;
  for (const [k, v] of m) s += ` ${k}=${v}`;
  return s;
}
function tally(keys: int[], d: int): Map<int, int> {
  const m = new Map<int, int>();
  for (const k of keys) {
    if (m.has(k)) m.set(k, m.get(k)! + d);
    else m.set(k, d);
  }
  return m;
}
// small keys, insertion order kept, negatives
console.log(show(tally([3, -1, 3, 0, -2147483648, 2147483647, -1, 3], 1)));
// a key past 32 bits in the middle: the table widens and keeps counting
console.log(show(tally([5, 6, 5, 2147483648, 5, -2147483649, 6, 9007199254740991], 2)));
// a value crossing 2^31 by addition
const big = new Map<int, int>();
for (let i = 0; i < 5; i++) {
  if (big.has(7)) big.set(7, big.get(7)! + 1000000000);
  else big.set(7, 1000000000);
}
console.log(show(big), big.get(7)!);
// a negative delta past -2^31
console.log(show(tally([1, 1, 1], -1500000000)));
// many keys (grows several times), then get, has, delete, set and iteration
const m = new Map<int, int>();
for (let i = 0; i < 5000; i++) {
  const k = (i * 7919) % 3001 - 1500;
  if (m.has(k)) m.set(k, m.get(k)! + 1);
  else m.set(k, 1);
}
let sum: int = 0;
for (const [k, v] of m) sum += k * 3 + v;
console.log(m.size, sum, m.get(0), m.has(-1500), m.has(5000));
m.delete(0);
m.delete(-1500);
m.set(123456789012, 5);
let first = "";
let n = 0;
for (const [k, v] of m) {
  if (n < 3) first += `${k}=${v} `;
  n++;
}
console.log(m.size, n, first, m.get(123456789012));
// clear and reuse
m.clear();
const again: int[] = [4, 4, 2];
for (const k of again) {
  if (m.has(k)) m.set(k, m.get(k)! + 1);
  else m.set(k, 1);
}
console.log(show(m));
// plain set of values past 32 bits
const p = new Map<int, int>();
p.set(1, 1);
p.set(2, 4294967296);
p.set(1, p.get(1)! + 1);
console.log(show(p), Array.from(p.keys()).join(","), Array.from(p.values()).join(","));
