// Map and Set over many sizes: inserts, updates, deletes, re-inserts (a deleted key comes back last), clear,
// tables grown and rebuilt with deleted slots, int and string keys, values of each kind. Iteration order is
// checked through an order-sensitive digest, and has/get against a plain array.
let seed: int = 12345;
function rnd(n: int): int {
  seed = (seed * 48271) % 2147483647; // (exact in a double too)
  return seed % n;
}
function digestInt(m: Map<int, int>): int {
  let d: int = 7;
  for (const [k, v] of m) d = (d * 31 + k * 7 + v) & 0xfffffff;
  return d;
}
function digestStr(m: Map<string, string>): int {
  let d: int = 7;
  for (const [k, v] of m) d = (d * 31 + k.length * 13 + Math.trunc(k.charCodeAt(k.length - 1)) + v.length) & 0xfffffff;
  return d;
}

// int keys against an array of values (-1: absent)
for (const range of [8, 100, 5000, 70000] as int[]) {
  const m = new Map<int, int>();
  const ref: int[] = new Array<int>(range).fill(-1);
  let ops: int = 0;
  for (let i: int = 0; i < range * 6; i++) {
    const slot = rnd(range);
    const k = slot * 977 - 3000; // negative keys too
    const op = rnd(10);
    if (op < 5) {
      if (m.has(k)) m.set(k, m.get(k)! + 1);
      else m.set(k, 1);
      ref[slot] = ref[slot] < 0 ? 1 : ref[slot] + 1;
    } else if (op < 8) {
      const had = m.delete(k);
      if (had !== ref[slot] >= 0) console.log("delete disagrees", k);
      ref[slot] = -1;
    } else if (m.has(k) !== ref[slot] >= 0 || (m.has(k) && m.get(k) !== ref[slot])) console.log("lookup disagrees", k);
    ops++;
  }
  let live: int = 0;
  for (const v of ref) if (v >= 0) live++;
  const keys = Array.from(m.keys());
  const values = Array.from(m.values());
  let vs: int = 0;
  for (const v of values) vs += v;
  console.log(range, ops, m.size, live, keys.length, vs, digestInt(m), m.size > 0 ? keys[0] : 0);
  m.clear();
  console.log(m.size, m.has(keys.length > 0 ? keys[0] : 0));
  for (let i: int = 0; i < 40; i++) m.set(i * 3, i);
  console.log(m.size, digestInt(m), m.get(39 * 3));
}

// re-insertion order: a deleted key that comes back is iterated last
const o = new Map<int, int>();
for (let i: int = 0; i < 20; i++) o.set(i, i);
for (let i: int = 0; i < 20; i += 3) o.delete(i);
o.set(0, 100);
o.set(5, 50); // an update keeps its place
let order = "";
for (const [k, v] of o) order += `${k}:${v} `;
console.log(order);
console.log(Array.from(o.entries()).length, Array.from(o.keys()).join(","));

// string keys, string values (held references), deletes and re-inserts
const s = new Map<string, string>();
for (let i: int = 0; i < 30000; i++) {
  const k = `key${rnd(9000)}`;
  const op = rnd(4);
  if (op === 0) s.delete(k);
  else s.set(k, `v${i}`);
}
console.log(s.size, digestStr(s), s.has("key17"), s.has("key17") ? s.get("key17")! : "none");
const firstKeys = Array.from(s.keys()).slice(0, 5).join(" ");
console.log(firstKeys);
s.clear();
s.set("again", "x");
console.log(s.size, Array.from(s.entries()).length, s.get("again"));

// sets
const set = new Set<int>();
for (let i: int = 0; i < 50000; i++) {
  const k = rnd(20000);
  if (rnd(3) === 0) set.delete(k);
  else set.add(k);
}
let sd: int = 0;
for (const k of set) sd = (sd * 31 + k) & 0xfffffff;
console.log(set.size, sd, set.has(5), set.has(19999));
const ss = new Set<string>(["b", "a", "c", "a"]);
ss.delete("b");
ss.add("b");
console.log(Array.from(ss).join(""), ss.size);

// values of each kind: number, boolean, objects
const mn = new Map<int, number>();
const mb = new Map<string, boolean>();
const mo = new Map<int, int[]>();
for (let i: int = 0; i < 3000; i++) {
  mn.set(i % 700, i / 4);
  mb.set(`b${i % 50}`, i % 3 === 0);
  if (!mo.has(i % 300)) mo.set(i % 300, []);
  mo.get(i % 300)!.push(i);
  if (i % 7 === 0) mo.delete((i * 13) % 300);
}
let tn = 0;
for (const v of mn.values()) tn += v;
let tb: int = 0;
for (const [_k, v] of mb) if (v) tb++;
let to: int = 0;
for (const [k, v] of mo) to = (to * 7 + k + v.length) & 0xfffffff;
console.log(mn.size, tn, mb.size, tb, mo.size, to);
