// for (const [k, v] of map) over the entries in place when the body cannot change the map (fasta's inner loop);
// break, continue and return leave it with nothing kept alive; a body that changes the map keeps the snapshot
const m = new Map<string, int>([["a", 1], ["b", 2], ["c", 3]]);
let s: int = 0;
for (const [k, v] of m) { if (k === "b") continue; s += v; if (v === 3) break; }
for (const [k, v] of m) { if (v === 1) m.set(k, 10); s += v; } // changes an existing entry: the snapshot path
const o = new Map<int, string>([[1, "x"], [2, "y"]]);
let t = "";
for (const [k, v] of o) t += `${k}${v}`;
function f(): int { let r: int = 0; for (const [_k, v] of m) { if (v > 2) return r; r += v; } return r; }
const objs = new Map<string, int[]>([["p", [1, 2]], ["q", [3]]]);
let n: int = 0;
for (const [_k, v] of objs) n += v.length;
console.log(s, t, f(), m.get("a"), n);
