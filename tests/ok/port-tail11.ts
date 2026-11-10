// appa round 11: shapes copied where wider-typed data goes (nullable arrays, Record rows, spreads), promise methods
// on an async call that gives T | null
const total = (xs: Array<{ linhas: number }> | null) => (xs == null ? null : xs.reduce((s, x) => s + x.linhas, 0));
const a: Array<{ empresa: string; linhas: number; valor: number }> | null = [
  { empresa: "A", linhas: 2, valor: 1.5 },
  { empresa: "B", linhas: 3, valor: 2 },
];
const b: Array<{ empresa: string; linhas: number }> | null = null;
console.log(total(a), total(b));

interface Fam { familia: string; dia: string; n: number | null }
function bulk(docs: Record<string, unknown>[]): void { console.log(JSON.stringify(docs)); }
const fs: Fam[] = [{ familia: "A", dia: "2026-11-10", n: 1 }, { familia: "B", dia: "2026-11-10", n: null }];
bulk(fs.map((f) => ({ ...f, tipo: "familia" })));
const filtros: Record<string, unknown>[] = [{ term: { x: 1 } }];
const must = [{ term: { doc: "d" } }, ...filtros];
console.log(JSON.stringify(must));

interface Ev { em: string; por: string }
const base: Ev[] = [{ em: "a", por: "b" }];
const mais = [{ em: "c", por: "d", extra: 1 }];
const todos: Ev[] = [...base, ...mais];
console.log(todos.length, todos[1].por);

class Lote { id = "a"; }
class Store {
  async le(id: string): Promise<Lote | null> {
    if (id === "x") throw new Error("boom");
    return id === "n" ? null : new Lote();
  }
}
const st = new Store();
async function main(): Promise<void> {
  const l1 = await st.le("a").catch(() => null);
  const l2 = await st.le("x").catch(() => null);
  const l3 = await st.le("n").catch(() => null);
  console.log(l1?.id ?? "none", l2?.id ?? "none", l3?.id ?? "none");
}
main();
