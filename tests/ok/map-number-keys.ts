const m = new Map<number, string>();
const ids: number[] = [3, 7, 3];
for (const id of ids) m.set(id, `v${id}`);
const s = new Set<number>(ids);
let total: number = 0;
for (const [k, v] of m) { total += k; console.log(k, v); }
console.log(m.size, s.size, s.has(7), m.get(3), total / 4);
