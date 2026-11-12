// a parameter with a default is optional in the function's type (b: R = {} reads as b?: R): passed where
// (s: string, b?: R) => ... goes, a call that leaves it out (or gives undefined) gets the default, evaluated in the
// function as JavaScript does; one that gives it passes the value
type Linha = Record<string, unknown>;
class Svc {
  async usa(rows: (sql: string, b?: Record<string, unknown>) => Promise<Linha[]>, x: string): Promise<number> {
    const a = await rows("select " + x);
    const c = await rows("select 2", { k: x });
    const u = await rows("select 3", undefined);
    return a.length * 100 + c.length * 10 + u.length;
  }
  sync(f: (s: string, n?: number) => string): string {
    return f("a") + "|" + f("b", 7) + "|" + f("c", 0);
  }
  dois(f: (s: string, n?: number, t?: string) => string): string {
    return f("a") + "|" + f("bb", 1) + "|" + f("ccc", undefined, "x") + "|" + f("d", 2, "y");
  }
  m(s: string, n: number = 3): string {
    return s + ":" + n;
  }
}
function decl(s: string, n: number = 5): string {
  return s + "=" + n;
}
function leDoAnterior(s: string, n: number = s.length, t: string = s + "!"): string {
  return s + "/" + n + "/" + t;
}
let chamadas = 0;
function conta(): number {
  chamadas++;
  return chamadas * 100;
}
async function main(): Promise<void> {
  const base = 2;
  const rows = async (sql: string, binds: Record<string, unknown> = {}): Promise<Linha[]> => {
    const out: Linha[] = [{ sql }];
    for (const k of Object.keys(binds)) out.push({ k, v: binds[k] });
    for (let i = 0; i < base; i++) out.push({ i });
    return out;
  };
  console.log((await rows("x")).length, (await rows("y", { a: 1 })).length);
  const s = new Svc();
  console.log(await s.usa(rows, "z"));
  const g = (s: string, n: number = base * 10): string => s + "#" + n;
  console.log(g("r", 1));
  console.log(s.sync(g));
  console.log(s.sync(decl));
  const h = (s: string, n: number = conta()): string => s + "@" + n;
  console.log(s.sync(h), chamadas);
  console.log(s.dois(leDoAnterior));
  console.log(decl("w"), s.m("v"), s.m("u", 9), leDoAnterior("ab"));
}
main();
