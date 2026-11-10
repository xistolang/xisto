interface Opts { onItem?: (n: number) => void; nome: string; }
class Coletor {
  private cb: ((n: number) => void) | null = null;
  constructor(o: Opts) { this.cb = o.onItem ?? null; }
  avisa(n: number): void {
    const cb = this.cb;
    if (cb) cb(n);
    this.cb?.(n * 10);
  }
}
const c1 = new Coletor({ nome: "a", onItem: (n: number): void => console.log("item", n) });
const c2 = new Coletor({ nome: "b" });
c1.avisa(1); c2.avisa(2);
console.log("fim");
class Fonte { conta: ((s: string) => number) | null = null; }
const f = new Fonte();
console.log(f.conta?.("abc") ?? -1);
f.conta = (s: string): number => s.length;
console.log(f.conta?.("abc") ?? -1);
