// XB-217: Map.get of a missing key with a primitive value is null (JavaScript's undefined) wherever the result can
// show it: any key or map expression before ??, a T | undefined slot (return, argument, field), === undefined, || d
const m = new Map<string, number>();
m.set("a", 2);
const ks = ["a", "b"];
function key(i: int): string { return ks[i]; }
for (let i: int = 0; i < 2; i++) console.log(m.get(ks[i]) ?? 0, m.get(key(i)) ?? 0, m.get(ks[i].slice(0)) ?? 0);
function maps(): Map<string, number> { return m; }
console.log(maps().get("zz") ?? 9, maps().get("a") ?? 9);
function r(k: string): number | undefined { return m.get(k); }
console.log(r("zz") === undefined, r("a"));
function p(x: number | undefined): string { return x === undefined ? "u" : String(x); }
console.log(p(m.get("zz")), p(m.get("a")));
console.log(m.get("zz") === undefined, m.get("a") !== undefined, m.get("zz") == null, m.get("a") != null);
console.log(m.get("zz") || 7, m.get("a") || 7);
const words = ["x", "y", "x", "z", "x"];
const counts = new Map<string, int>();
for (const w of words) counts.set(w, (counts.get(w) || 0) + 1);
console.log([...counts.entries()]);
const flags = new Map<int, boolean>();
flags.set(1, true);
for (let i: int = 0; i < 3; i++) console.log(i, flags.get(i) ?? false, flags.get(i) === undefined, flags.get(i) ? "on" : "off");
const o: { n?: number } = {};
o.n = m.get("zz");
console.log(o.n === undefined);
o.n = m.get("a");
console.log(o.n);
if (!flags.get(5)) console.log("not set");
