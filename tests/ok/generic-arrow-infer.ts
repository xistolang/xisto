// a generic arrow written inside a function, called without type arguments: T inferred from the arguments as
// TypeScript infers it (a Map<string, string[]> where Map<string, T[]> goes is that Map itself, a callback's
// parameter is T, the result written with T is read as T: the same objects); new Map(xs.map(([k, l]) => [k, f(l)]))
// with a destructured parameter makes its entries pairs
interface X { a: number; b: string | null; }
interface I { a: number; }
class C implements I { a = 1; b = 2; }
function main(): void {
  const xs: X[] = [{ a: 1, b: "p" }, { a: 2, b: null }, { a: 1, b: "q" }];
  const exemplos: string[] = [];
  const conta = <T>(v2: Map<string, T[]>, v1: Map<string, T[]>, igual: (a: T, b: T) => boolean, rotulo: string) => {
    let n = 0;
    for (const [k, a] of v2) {
      const b = v1.get(k) ?? [];
      for (let i = 0; i < a.length && i < b.length; i++) {
        if (igual(a[i], b[i])) n++;
        else exemplos.push(`${rotulo}:dif:${k}`);
      }
    }
    return { rotulo, n };
  };
  const agrupa = <T>(ys: readonly T[], k: (x: T) => string): Map<string, T[]> => {
    const m = new Map<string, T[]>();
    for (const y of ys) m.set(k(y), [...(m.get(k(y)) ?? []), y]);
    return m;
  };
  const primeiro = <T>(ys: T[]): T => ys[0];
  const sig2 = (c: X) => [String(c.a), c.b ?? "-"].join("|");
  const m2 = agrupa(xs, (c) => String(c.a));
  console.log(m2.get("1")!.length, m2.get("1")![0] === xs[0], m2.get("2")![0].b);
  const iguais = new Map<string, string[]>();
  for (const [k, l] of m2) iguais.set(k, l.map(sig2));
  iguais.set("2", ["2|x"]);
  const novo = new Map([...m2].map(([k, l]) => [k, l.map(sig2)]));
  console.log(novo);
  console.log(conta(novo, iguais, (a, b) => a === b, "cob"));
  console.log(conta(new Map([...m2].map(([k, l]) => [k, l.map(sig2)])), iguais, (a, b) => a === b, "cob2"));
  console.log(conta(m2, m2, (a, b) => a.a === b.a, "obj"));
  console.log(exemplos);
  const n1 = new Map([...m2].map(([k, l]) => [k, l.length]));
  console.log(n1);
  const ss = ["s", "t"];
  const is: I[] = [new C(), { a: 3 }];
  const p = primeiro(is);
  console.log(p.a, p === is[0], primeiro(ss).length, primeiro(ss) === "s");
  const mi = agrupa(is, (x) => String(x.a));
  console.log(mi.get("3")![0] === is[1], [...mi.keys()]);
}
main();
