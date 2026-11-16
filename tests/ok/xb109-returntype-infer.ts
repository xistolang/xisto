// XB-109: ReturnType<typeof arrow> with an inferred return type, named before the arrow and the constants it reads
type M = ReturnType<typeof mk>;
type Row = ReturnType<typeof toRow>;
function use(m: M): string { return `${m.s}:${m.n}`; }
const BASE = 10;
const mk = (n: number) => ({ n: n + BASE, s: `x${n}`, tags: [n] });
const cfg = { m: mk(1) };
console.log(use(mk(2)), cfg.m.n, cfg.m.tags.length);
type Q = ReturnType<typeof q>;
const q = () => cfg;
const qq: Q = q();
console.log(qq.m.s);
const rows: Row[] = [];
function add(n: number): Row { const r = toRow(n); rows.push(r); return r; }
const toRow = (n: number) => ({ id: n, label: PREFIX + String(n), when: SCALE * n });
const PREFIX = "row-";
const SCALE = 1.5;
add(1); add(2);
console.log(rows.map((r) => `${r.label}/${r.when}`).join(" "));
