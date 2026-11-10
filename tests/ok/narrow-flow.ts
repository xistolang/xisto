// narrowing as TypeScript's flow reads it: a?.f compared, if (!x) { x = ... }, x = <not null>
interface Ctx { k5: string | null; tag: string; erros: string[] | null }
function a(ctx: Ctx | null): string { if (ctx?.k5) return ctx.k5; return "none"; }
function b(e: Ctx | null): boolean { return e?.tag === "X" && e.erros !== null; }
function c(r: Ctx | null): boolean { if (r?.tag === "k" && r.k5 === null) return true; return false; }
function d(r: Ctx | null): number { if (r?.erros != null) return r.erros.length; return -1; }
console.log(a({ k5: "v", tag: "t", erros: null }), a(null), b({ k5: null, tag: "X", erros: [] }), b(null));
console.log(c({ k5: null, tag: "k", erros: null }), c(null), d({ k5: null, tag: "", erros: ["a", "b"] }), d(null));

class Slot { v: Map<string, number> | null = null; ler(): Map<string, number> | null { return this.v; } gravar(m: Map<string, number>): void { this.v = m; } }
class Ondas {
  slot = new Slot();
  put(k: string, n: number): number {
    let mapa = this.slot.ler();
    if (!mapa) { mapa = new Map(); this.slot.gravar(mapa); }
    const existente = mapa.has(k) ? mapa.get(k) : null;
    mapa.set(k, n);
    return existente ?? -1;
  }
}
const o = new Ondas();
console.log(o.put("a", 1), o.put("a", 2), o.put("b", 3));

let s: string | null = null;
s = "first";
console.log(s.length, s != null);

// n ?? '' (number | null): the number as text
function txt(n: number | null, i: int | null): string { return `[${n ?? ""}|${i ?? "-"}]`; }
console.log(txt(2.5, 3), txt(null, null));

// query<T = any>(): T from the type the result goes to, or given explicitly
interface Count { N: number; NOME: string }
class Ds {
  async query<T = any>(sql: string): Promise<T> {
    const raw: object = JSON.parse('[{"N":3,"NOME":"x"}]');
    const rows: T = raw;
    return rows;
  }
}
async function main(): Promise<void> {
  const ds = new Ds();
  const rows: Count[] = await ds.query("select");
  console.log(rows.length, rows[0].N, rows[0].NOME);
  const r3 = await ds.query<Count[]>("select");
  console.log(r3[0].NOME);
}
main();
