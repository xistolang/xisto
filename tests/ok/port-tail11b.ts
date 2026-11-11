// appa round 11: new Map(map) / Object.fromEntries(map), typeof "bigint" narrowing a union (and typeof of a boxed
// bigint), Array.isArray narrowing through a || chain and after an early return
const m = new Map<string, string>([["a", "1"], ["b", "2"]]);
const o = Object.fromEntries(m);
console.log(JSON.stringify(o));
const r: Record<string, string> = Object.fromEntries(m);
console.log(JSON.stringify(r), new Map(m).size, new Set(new Set([1, 2, 2])).size);
function de(v: number | string | bigint): string {
  if (typeof v === "bigint") return "big " + v.toString();
  const t = typeof v === "number" ? String(v) : v.trim();
  return t;
}
console.log(de(5n), de(3), de(" x "));
function kind(v: number | string | bigint): string { return typeof v; }
console.log(kind(5n), kind(3), kind("a"));
function cont(rows: unknown, n: number): number[] | null {
  if (!Array.isArray(rows) || rows.length !== 1 || rows[0] == null) return null;
  const linha = rows[0] as Record<string, unknown>;
  const out: number[] = [];
  for (let i = 1; i <= n; i++) out.push(Number(linha[`C${i}`] ?? 0));
  return out;
}
console.log(cont([{ C1: 2, C2: 3 }], 2), cont("x", 1), cont([], 1));
function both(x: unknown, y: string | null): string {
  if (!Array.isArray(x) || y === null) return "no";
  return `${x.length} ${y.length}`;
}
console.log(both([1, 2], "abc"), both([1], null), both(3, "a"));
// spreads of T | null and of optional fields: a null or a left-out field keeps the key's earlier value
interface Imp { frequencia?: string; volume?: string; umAUm?: string }
interface Item { id: string; impacto?: Imp; estado: string }
function mk(id: string, extra: Partial<Item> = {}): Item {
  return { id, estado: "virado", ...extra, impacto: { frequencia: "por autorização", umAUm: "igual", ...extra.impacto } };
}
for (const it of [mk("a"), mk("b", { estado: "novo", impacto: { volume: "10" } }), mk("c", { impacto: { frequencia: "mensal" } })])
console.log(it.id, it.estado, it.impacto?.frequencia, it.impacto?.volume ?? "-", it.impacto?.umAUm);
// Array.isArray on an any keeps it any (any[]); a callback that takes no parameter
function anoDe(rows: any): number {
  if (!Array.isArray(rows) || rows.length !== 1) return -1;
  return Number(rows[0]?.CD_ANO) || 0;
}
console.log(anoDe(JSON.parse('[{"CD_ANO": 2020}]')), anoDe(JSON.parse("[]")), anoDe(3));
interface Res { ok: number }
const nums = [1, 2, 3];
const res: (Res | null)[] = nums.map((): Res | null => null);
res[1] = { ok: 5 };
console.log(res.map((r) => r?.ok ?? -1), nums.filter(() => true).length);
// a null test on a name a typeof narrowed; [object, Record] as Record[]
function lenOr(x: string | number | null, o: unknown): number {
  return typeof x === "number" ? 1 : x === null || o === null ? 2 : x.length;
}
function same(x: string | number | null, o: unknown): boolean {
  return typeof x === "number" ? true : x === null || o === null ? false : x === o;
}
console.log(lenOr("abc", 1), lenOr(null, 1), lenOr(3, 1), same("a", "a"), same("a", "b"));
function exato(c: string, v: string): Record<string, unknown> { return { term: { [c]: v } }; }
const filtro = [{ term: { doc: "d" } }, exato("runId", "r1")];
console.log(JSON.stringify(filtro));
