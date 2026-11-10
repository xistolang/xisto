type V = string | number | Date | null;
const r: Record<string, V> = { a: "x", b: 2, c: null, d: new Date(0) };
for (const k of Object.keys(r)) { const v = r[k]; console.log(k, v === null ? "null" : typeof v); }
const m = new Map<string, string | null>();
m.set("a", null); m.set("b", "y");
console.log(m.get("a"), m.get("b"), m.has("a"));
