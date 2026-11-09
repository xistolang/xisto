// XB-088: for (const [k, v] of map) over a map the body does not change: k and v borrow the entry (no counts move);
// a use that keeps one (a store, a closure) takes its own reference, as with any local.
const table = new Map<string, number>([["a", 0.25], ["c", 0.5], ["g", 0.75], ["t", 1]]);
const picked: string[] = [];
const later: (() => string)[] = [];
let seed = 1;
for (let i = 0; i < 20; i++) {
  seed = (seed * 75 + 74) % 65537;
  const r = seed / 65537;
  for (const [c, p] of table) {
    if (r < p) {
      picked.push(c);
      if (i % 7 === 0) later.push(() => c + c);
      break;
    }
  }
}
console.log(picked.join(""), later.map((f) => f()).join(","));
const names = new Map<string, string>([["x", "ex"], ["y", "why"]]);
let longest = "";
for (const [k, v] of names) if (v.length > longest.length) longest = k + "=" + v;
console.log(longest);
