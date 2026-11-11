// `implements` with a signature that is not the interface's but fits it (TypeScript accepts it: a wider parameter,
// one more optional parameter, a narrower result) next to classes that keep the exact signature (those stay plain
// subclasses of the interface; the others are adapted where the interface goes, XB-126)
interface Entrada {
  valor: number;
  taxa: number;
}
interface Calculo {
  executa(e: Entrada): Promise<number>;
  nome(): string;
}
class EntradaRica {
  valor!: number;
  taxa!: number;
  arredonda?: boolean;
}
class Exato implements Calculo {
  async executa(e: Entrada): Promise<number> {
    return e.valor * e.taxa;
  }
  nome(): string {
    return "exato";
  }
}
class Largo implements Calculo {
  async executa(e: EntradaRica, escala?: number): Promise<number> {
    const v = e.valor * e.taxa * (escala ?? 1);
    return e.arredonda === true ? Math.round(v) : v;
  }
  nome(): string {
    return "largo";
  }
}
class MaisLargo extends Largo {
  nome(): string {
    return "mais-largo";
  }
}
class SemDeclarar {
  executa(e: Entrada): number {
    return e.valor + e.taxa;
  }
  nome(): string {
    return "sem";
  }
}
async function total(cs: Calculo[]): Promise<number> {
  let t = 0;
  for (const c of cs) {
    const v = await c.executa({ valor: 2, taxa: 1.25 });
    console.log(c.nome(), v);
    t += v;
  }
  return t;
}
async function main(): Promise<void> {
  const largo = new Largo();
  console.log(await largo.executa({ valor: 2, taxa: 1.25, arredonda: true }, 3));
  console.log(await total([new Exato(), largo, new MaisLargo(), new SemDeclarar()]));
}
main();
