// An interface that declares methods, implemented by classes: a variable of the interface's type calls the
// implementation it holds (the legacy/modern pattern of a contract with two implementations).
interface Entrada {
  valor: number;
  taxa: number;
}

interface Calculo {
  executa(e: Entrada): Promise<number>;
  nome(): string;
}

class CalculoLegado implements Calculo {
  async executa(e: Entrada) { return e.valor * e.taxa; }
  nome() { return "legado"; }
}

class CalculoNovo implements Calculo {
  private readonly arredonda: boolean;
  constructor(arredonda: boolean) { this.arredonda = arredonda; }
  async executa(e: Entrada): Promise<number> {
    const v = e.valor * e.taxa;
    if (this.arredonda) return Math.round(v);
    return v;
  }
  nome(): string { return this.arredonda ? "novo-redondo" : "novo"; }
}

class Servico {
  private readonly calc: Calculo;
  constructor(calc: Calculo) { this.calc = calc; }
  async roda(valor: number) {
    const r = await this.calc.executa({ valor, taxa: 1.5 });
    return `${this.calc.nome()}=${r}`;
  }
}

async function main(): Promise<void> {
  const impls: Calculo[] = [new CalculoLegado(), new CalculoNovo(true), new CalculoNovo(false)];
  for (const c of impls) console.log(await new Servico(c).roda(3));
  const escolhido: Calculo = impls.length > 2 ? impls[1] : impls[0];
  console.log(escolhido.nome(), await escolhido.executa({ valor: 5, taxa: 0.5 }));
}
main();
