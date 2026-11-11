// An interface with methods is structural (XB-126): a class that has its methods with signatures that fit is one,
// without `implements`. Wider parameters (a class of data with more, optional, fields), a narrower result (more
// fields), one more optional parameter, a plain result where the interface returns a promise, async methods, T | null,
// arrays and fields of the interface, `c as I` and console.log of an adapted value. xisto wraps the object in an adapter that calls its methods.
interface Pedido {
  empresa: string;
  quantidade: number;
}
interface Resultado {
  id: string;
  total: number;
}
interface Motor {
  executa(p: Pedido, fator: number): Resultado;
  nome(): string;
}
interface MotorAsync {
  roda(p: Pedido, opts: { runId: string; silencioso?: boolean }): Promise<Resultado>;
}
class PedidoCompleto {
  empresa!: string;
  quantidade!: number;
  observacao?: string;
}
interface Opcoes {
  runId: string;
  silencioso?: boolean;
  aoPasso?: (n: number) => void;
}
class MotorReal {
  private contador = 0;
  executa(p: PedidoCompleto, fator: number, extra?: string): { id: string; total: number; detalhe: string } {
    this.contador++;
    return { id: `${p.empresa}-${this.contador}`, total: p.quantidade * fator, detalhe: extra ?? "sem extra" };
  }
  nome(): string {
    return "real";
  }
  outro(): number {
    return this.contador;
  }
  async roda(p: PedidoCompleto, opts?: Opcoes): Promise<{ id: string; total: number; linhas: number[] }> {
    const id = opts?.runId ?? "?";
    if (opts?.silencioso !== true) console.log(`rodando ${id} para ${p.empresa}`);
    return { id, total: p.quantidade, linhas: [1, 2] };
  }
}
class Sincrono {
  roda(p: Pedido, opts: { runId: string; silencioso?: boolean }): Resultado {
    return { id: opts.runId, total: p.quantidade * 10 };
  }
}
class Painel {
  atual: Motor | null = null;
  motores: Motor[] = [];
}
function usa(m: Motor): void {
  const r = m.executa({ empresa: "A1", quantidade: 3 }, 2);
  console.log(m.nome(), r.id, r.total);
}
async function usaAsync(m: MotorAsync, n: number): Promise<number> {
  const r = await m.roda({ empresa: "B2", quantidade: n }, { runId: `r${n}`, silencioso: n > 1 });
  return r.total;
}
async function main(): Promise<void> {
  const real = new MotorReal();
  usa(real);
  usa(real);
  console.log(real.outro());
  const m: Motor = real;
  console.log(m.executa({ empresa: "C", quantidade: 1 }, 1.5).total);
  console.log(await usaAsync(real, 1), await usaAsync(real, 2), await usaAsync(new Sincrono(), 3));
  const p = new Painel();
  const talvez: MotorReal | null = real.outro() > 0 ? real : null;
  p.atual = talvez;
  console.log(p.atual === null ? "nenhum" : p.atual.nome());
  const nada: MotorReal | null = null;
  p.atual = nada;
  console.log(p.atual === null ? "nenhum" : p.atual.nome());
  p.motores.push(real);
  const lista: Motor[] = [real, new MotorReal()];
  for (const x of lista) console.log(x.executa({ empresa: "D", quantidade: 2 }, 1).id);
  console.log(p.motores.length, real.outro());
  const comoMotor = new Sincrono() as MotorAsync; // c as I: the same adapter
  console.log((await comoMotor.roda({ empresa: "E", quantidade: 1 }, { runId: "as" })).id);
  console.log(comoMotor, p.motores); // an adapter prints as the object it holds
}
main();
