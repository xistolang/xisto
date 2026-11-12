// type operators read by the checker: T[K] with an alias of keys, T[keyof T], mapped types (over an object, a tuple,
// keys), ReturnType<C['m']>, Awaited<...>, Pick<G, Alias>, A & B with a Record or a generic side, typeof s & {...} in
// a predicate, ConstructorParameters<typeof C>[0]; new Array(n) typed by its declaration, an async function
// expression, push with several spreads, a parameter typed by its default
// build: --tsc-checked
type Modo = 'LE' | 'ESCREVE';
interface Ciclo { modo: Record<Modo, 'desligado' | 'processado'>; n: number }
function estado(m: Modo): Ciclo['modo'][Modo] { return m === 'LE' ? 'processado' : 'desligado'; }

interface Tot { a: number; b: number }
function maior(t: Tot): Tot[keyof Tot] { return Math.max(t.a, t.b); }

class Repo {
  async carrega(id: number): Promise<{ id: number; nome: string }> { return { id, nome: 'n' + id }; }
}
type Carga = Awaited<ReturnType<Repo['carrega']>>;

interface Guia { id: number; valor: number; nome: string }
type Campo = 'valor' | 'nome';
function atualiza(g: Guia, campos: Partial<Pick<Guia, Campo>>): void {
  if (campos.valor != null) g.valor = campos.valor;
  if (campos.nome != null) g.nome = campos.nome;
}
type Flags = { [K in 'x' | 'y']: boolean };

type Bucket = { key: string; doc_count: number } & Record<string, { value?: number }>;

interface Rodada { familias: Array<{ nome: string; divergencias: string[] }> }
type Resumo = Array<Omit<Rodada['familias'][number], 'divergencias'> & { divergencias: number }>;

function junta<R extends { id: number }, F extends { n: string }>(rs: Array<R & { fam: F[] }>): F[] {
  const out: F[] = [];
  for (const r of rs) out.push(...r.fam);
  return out;
}

interface Marca { m: string | null }

class Cli {
  url: string;
  constructor(o: { url?: string; auth?: { user: string } }) { this.url = (o.url ?? '-') + (o.auth ? '@' + o.auth.user : ''); }
}

const cfg = () => ({ n: 4 });
function vezes(x: number, n = cfg().n): number { return x * n; }
function soma(opts: { lim?: number }): number {
  const h = (x: number, lim = opts.lim) => x + (lim ?? 0);
  return h(1);
}

async function main(): Promise<void> {
  console.log(estado('LE'), estado('ESCREVE'), maior({ a: 3, b: 7 }));
  const c: Carga = await new Repo().carrega(3);
  console.log(c.id, c.nome);
  const medidas = { a: Promise.resolve(1), b: Promise.resolve('x') };
  const monta = (m: { [K in keyof typeof medidas]: Awaited<(typeof medidas)[K]> }): string => `${m.a}-${m.b}`;
  console.log(monta({ a: await medidas.a, b: await medidas.b }));
  const g: Guia = { id: 1, valor: 10, nome: 'g' };
  atualiza(g, { nome: 'h' });
  console.log(g.valor, g.nome);
  const fl: Flags = { x: true, y: false };
  console.log(fl.x, fl.y);
  const bs = JSON.parse('[{"key":"d1","doc_count":2,"ok":{"value":5}}]') as Bucket[];
  for (const b of bs) console.log(b.key, b.doc_count + 1, b.ok.value);
  const res: Resumo = [{ nome: 'f', divergencias: 2 }];
  console.log(res[0].nome, res[0].divergencias);
  console.log(junta([{ fam: [{ n: 'a' }, { n: 'b' }], id: 1 }, { fam: [{ n: 'c' }], id: 2 }]).map((f) => f.n).join(','));
  const opts: Record<string, unknown> = { url: 'http://x' };
  opts.auth = { user: 'u' };
  console.log(new Cli(opts as ConstructorParameters<typeof Cli>[0]).url);
  const xs: Marca[] = [{ m: 'abc' }, { m: null }];
  console.log(xs.filter((s): s is typeof s & { m: string } => s.m != null).map((s) => s.m.length).join(','));
  const nomes: string[] = new Array(3);
  nomes[0] = 'z';
  console.log(nomes.length, nomes[0]);
  const dobra = async function dobro(x: number): Promise<number> { return x * 2; };
  console.log(await dobra(4));
  const o: { xs?: number[] } = {};
  const um = [1];
  (o.xs ??= []).push(...um, 5, ...[6, 7]);
  console.log(o.xs.join(','));
  console.log(vezes(2), soma({ lim: 5 }), soma({}));
}
main();
