// the counting idiom on a Map<int, int>, fused by the emitter into one probe: present and absent keys,
// literal and named deltas (negative too), the sum written either way round, and shapes that must not fuse
function count(xs: int[]): Map<int, int> {
  const m = new Map<int, int>();
  for (const x of xs) {
    if (m.has(x)) m.set(x, m.get(x)! + 1);
    else m.set(x, 1);
  }
  return m;
}
function weigh(xs: int[], w: int[]): Map<int, int> {
  const m = new Map<int, int>();
  for (let i = 0; i < xs.length; i++) {
    const k = xs[i], d = w[i];
    if (m.has(k)) {
      m.set(k, d + m.get(k)!);
    } else {
      m.set(k, d);
    }
  }
  return m;
}
class Tally {
  hits: Map<int, int>;
  constructor() { this.hits = new Map<int, int>(); }
  add(k: int, d: int): void {
    if (this.hits.has(k)) this.hits.set(k, this.hits.get(k)! + d - 1 + 1);
    else this.hits.set(k, d);
  }
}
function show(m: Map<int, int>): string {
  const ks = Array.from(m.keys()).sort((a: int, b: int) => a - b);
  let s = "";
  for (const k of ks) s += `${k}:${m.get(k)!} `;
  return s;
}
const a = count([3, 1, 3, 2, 3, 1, -7, 0, 0]);
console.log(show(a), a.size);
const b = weigh([5, 5, 6, 5, 6, 7], [10, -3, 4, 1, -4, 0]);
console.log(show(b), b.size);
const t = new Tally();
t.add(1, 2); t.add(1, 3); t.add(2, -1); t.add(2, -1);
console.log(show(t.hits));
// not the idiom: different key in the else, a product, a different map: plain code, same answers
const c = new Map<int, int>();
const other = new Map<int, int>();
const xs3: int[] = [1, 2, 1];
for (const x of xs3) {
  if (c.has(x)) c.set(x, c.get(x)! * 2);
  else c.set(x + 1, 1);
  if (c.has(x)) other.set(x, c.get(x)! + 1);
  else other.set(x, 1);
}
console.log(show(c), show(other));
