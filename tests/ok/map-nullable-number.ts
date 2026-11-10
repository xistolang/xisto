const m = new Map<string, number | null>();
m.set("a", null); m.set("b", 2.5);
console.log(m.get("a"), m.get("b"), m.has("a"), m.size);
for (const [k, v] of m) console.log(k, v === null ? -1 : v + 1);
