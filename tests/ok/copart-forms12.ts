// read as TypeScript reads them: G<A> where G<any> goes through the class's own __xs_any() (a package's Repository keeps its
// entity by name), Object.getPrototypeOf(u) === Object.prototype on an unknown (true for an object literal or a Record),
// new Set of string | undefined elements, an object half made when a field's value throws (let go, not leaked),
// Promise.race of p.then(ok, err) with handlers of different shapes and a third shape (their union), { tipo: r.tipo, ms }
// where the union's member is the one its kind names
// build: --tsc-checked
class Repo<T> {
  nome: string;
  fixo?: string;
  constructor(nome: string) {
    this.nome = nome;
  }
  conta(): number {
    return (this.fixo ?? this.nome).length;
  }
  __xs_any(): Repo<any> {
    const r = new Repo<any>(this.nome);
    r.fixo = this.nome; // (the same entity, kept by name)
    return r;
  }
}
class Usuario {
  id = 1;
}
class Senha {
  cd = 2;
}
class Svc {
  usuarios: Repo<Usuario> | null = null;
  senhas: Repo<Senha> = new Repo<Senha>('TB_SENHA');
  conta(): number {
    const repo: Repo<any> = this.usuarios ?? this.senhas;
    return repo.conta();
  }
}
const svc = new Svc();
console.log(svc.conta());
svc.usuarios = new Repo<Usuario>('TB_USUARIO');
console.log(svc.conta());

class C {
  a = 1;
}
function plano(valor: unknown): string {
  if (typeof valor === 'object' && valor !== null) {
    const proto = Object.getPrototypeOf(valor) as object | null;
    const ehPlano = proto === null || proto === Object.prototype;
    return ehPlano ? 'plano' : 'classe';
  }
  return 'prim';
}
const rec: Record<string, unknown> = { x: 1 };
console.log(plano({ a: 1 }), plano(new C()), plano(rec), plano([1]), plano(new Date(0)), plano(3));

const empresaDoPedido = new Map<number, string>([[1, 'a'], [2, 'b']]);
const linhas = [{ s: 1 }, { s: 2 }, { s: 1 }, { s: 3 }, { s: 4 }];
const empresas = new Set(linhas.map((l) => empresaDoPedido.get(l.s)));
console.log(empresas.size, empresas.has('a'), empresas.has(undefined));

type Item<T> = { ok: true; valor: T } | { ok: false; erro: unknown };
async function lanca<T>(fn: () => Promise<T>): Promise<Item<T>> {
  try {
    return { ok: true, valor: await fn() };
  } catch (erro) {
    return { ok: false, erro };
  }
}

type MotivoSolta = 'teto' | 'posse-perdida' | 'desligamento';
interface Res { n: number }
type Medicao =
| { tipo: 'ok'; res: Res; ms: number }
| { tipo: 'erro'; err: unknown; ms: number }
| { tipo: 'teto'; ms: number }
| { tipo: 'posse-perdida'; ms: number }
| { tipo: 'desligamento'; ms: number };
async function motor(falha: boolean, demora: number): Promise<Res> {
  await new Promise<void>((ok) => setTimeout(ok, demora));
  if (falha) throw new Error('motor');
  return { n: 42 };
}
async function mede(falha: boolean, demora: number, teto: number): Promise<Medicao> {
  const medida = motor(falha, demora);
  let timer: ReturnType<typeof setTimeout> | undefined;
  const sentinela = new Promise<MotivoSolta>((resolve) => {
    timer = setTimeout(() => resolve('teto'), teto);
  });
  const r = await Promise.race([
    medida.then(
      (res) => ({ tipo: 'ok' as const, res }),
      (err: unknown) => ({ tipo: 'erro' as const, err }),
    ),
    sentinela.then((motivo) => ({ tipo: motivo })),
  ]);
  void timer;
  const ms = 0;
  if (r.tipo === 'ok' || r.tipo === 'erro') {
    if (r.tipo === 'erro') return { tipo: 'erro', err: r.err, ms };
    return { tipo: 'ok', res: r.res, ms };
  }
  void medida.then(() => {}, () => {});
  return { tipo: r.tipo, ms };
}
async function main(): Promise<void> {
  const l = await lanca(async (): Promise<number> => {
    throw new Error("x");
  });
  console.log(l.ok ? l.valor : (l.erro as Error).message);
  const a = await mede(false, 5, 200);
  const b = await mede(true, 5, 200);
  const c = await mede(false, 200, 5);
  console.log(a.tipo, a.tipo === 'ok' ? a.res.n : '', b.tipo, b.tipo === 'erro' ? (b.err as Error).message : '', c.tipo);
}
main();
