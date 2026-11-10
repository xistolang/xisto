const store = new Map<string, number>();
export const Contador = {
  nome: "contador",
  limite: 3,
  /** soma */
  soma(a: number, b: number): number {
    return a + b + this.limite;
  },
  run<T>(fn: () => T): T {
    return fn();
  },
  async espera(n: number): Promise<number> {
    return n * 2;
  },
  digitos(s: string): string {
    return s.replace(/\D/g, "");
  },
  guarda(k: string): void { store.set(k, (store.get(k) ?? 0) + 1); },
};
console.log(Contador.nome, Contador.soma(1, 2), Contador.run((): string => "ok"), Contador.digitos("a1b2"));
Contador.guarda("x"); Contador.guarda("x");
console.log(store.get("x"));
async function main(): Promise<void> { console.log(await Contador.espera(21)); }
main();
