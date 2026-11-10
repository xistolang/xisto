interface Proc { readonly rotina: string; roda(n: number): Promise<number>; }
class A implements Proc { readonly rotina = "A"; async roda(n: number): Promise<number> { return n * 2; } }
class B implements Proc { rotina: string; constructor() { this.rotina = "B"; } async roda(n: number): Promise<number> { return n + 1; } }
async function main(): Promise<void> {
  const ps: Proc[] = [new A(), new B()];
  for (const p of ps) console.log(p.rotina, await p.roda(10));
}
main();
