// unknown as I, I an interface with methods (XB-126): an object of a decorated class (one with a ClassInfo, as an
// injector meets it) that fits I without implementing it is wrapped in an adapter (an object of no such class is a TypeError, where
// Node's cast checks nothing).
// (iface-shape-cast.expected is what Node prints for this program without the decorator lines.)
function Injectable(): (c: unknown) => void {
  return (c: unknown) => {};
}
interface Motor {
  executa(p: { empresa: string; quantidade: number }, fator: number): Promise<{ id: string; total: number }>;
  nome(): string;
}
class Pedido {
  empresa!: string;
  quantidade!: number;
  dryRun?: boolean;
}
interface Resultado {
  id: string;
  total: number;
  extra: string;
}
@Injectable()
class MotorReal {
  async executa(p: Pedido, fator: number, opts?: { log: boolean }): Promise<Resultado> {
    return { id: p.empresa, total: p.quantidade * fator, extra: opts === null ? "-" : "x" };
  }
  nome(): string {
    return "real";
  }
}
@Injectable()
class Exato implements Motor {
  async executa(p: { empresa: string; quantidade: number }, fator: number): Promise<{ id: string; total: number }> {
    return { id: `exato-${p.empresa}`, total: fator };
  }
  nome(): string {
    return "exato";
  }
}
function pega(v: unknown): Motor {
  return v as Motor;
}
function talvez(v: unknown): Motor | null {
  return v as Motor | null;
}
async function main(): Promise<void> {
  const vs: unknown[] = [new MotorReal(), new Exato()];
  for (const v of vs) {
    const m = pega(v);
    const r = await m.executa({ empresa: "Z", quantidade: 4 }, 2);
    console.log(m.nome(), r.id, r.total);
  }
  console.log(talvez(null) === null);
}
main();
