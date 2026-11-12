// a function whose result type is left out, every return an object literal (a shape the compiler makes), where a
// function returning an interface of the same fields goes: accepted as TypeScript accepts it (each result copied
// into the interface: a new object no one else has, so nothing can tell)
// build: --tsc-checked
interface Doc { id: number; nome: string; }
interface BancoDoDia {
  docs: Doc[];
  oracle: Map<string, { linhas: number; valor: number }> | null;
}
interface Par { a: string; n: number; }
class Svc {
  private ultima = new Map<string, BancoDoDia>();
  async bancoDoDia(chave: string, mede: () => Promise<BancoDoDia>): Promise<BancoDoDia> {
    const v = await mede();
    if (v.oracle) this.ultima.set(chave, v);
    return v;
  }
  par(f: (s: string) => Par): string {
    const p = f("x");
    return p.a + p.n;
  }
  tem(chave: string): boolean {
    return this.ultima.has(chave);
  }
}
async function main(): Promise<void> {
  const s = new Svc();
  const lista: Doc[] = [{ id: 1, nome: "um" }];
  for (const falha of [false, true]) {
    const banco = async () => {
      const docs = lista.filter((d) => d.id > 0);
      if (docs.length === 0) return { docs, oracle: new Map<string, { linhas: number; valor: number }>() };
      try {
        if (falha) throw new Error("caiu");
        const oracle = new Map<string, { linhas: number; valor: number }>();
        oracle.set("A", { linhas: 2, valor: 3.5 });
        return { docs, oracle };
      } catch (err) {
        console.log("erro", (err as Error).message);
        return { docs, oracle: null };
      }
    };
    const { docs, oracle } = await s.bancoDoDia("d" + falha, banco);
    console.log(docs === lista ? "mesma" : "outra", docs[0] === lista[0], docs.length, oracle ? oracle.get("A") : null, s.tem("d" + falha));
  }
  const mk = (s: string) => ({ a: s + "!", n: s.length });
  console.log(s.par(mk));
}
main();
