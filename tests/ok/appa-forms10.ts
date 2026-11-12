// read as TypeScript reads them: keyof T as the union of T's field names, T inferred through an object argument's fields,
// an array where an Iterable<T> goes, Object.fromEntries of [k, v] pairs, [...unknowns] where Record[] goes, a ?: whose
// value is dropped, [...new Set(numbers)], COLUNA[c] after `as Campo[]`, BigInt(number | bigint), ...(c ? {a} : {b}),
// R[i] != null with i a number, Promise.all([p, ...ps]), returns { x } and { x: null }, an exhaustive switch,
// Array.isArray(p?.xs) ? p.xs : [], T | Promise<T> returning T, overrides through a bridge, a Record of functions,
// delete o.k of an optional field
// build: --tsc-checked
interface Descartes { total: number; semLinha: number; erro: number }
type Motivo = 'semLinha' | 'erro';
const CONT: Record<Motivo, keyof Descartes> = { semLinha: 'semLinha', erro: 'erro' };
function conta(d: Descartes, m: Motivo): void { d.total++; d[CONT[m]]++; }
const ds: Descartes = { total: 0, semLinha: 0, erro: 0 };
conta(ds, 'erro'); conta(ds, 'semLinha');
console.log(JSON.stringify(ds));

interface Coluna<T> { nome: string; valor: (l: T) => unknown }
interface Linha { a: number; b: string }
const COLS: Coluna<Linha>[] = [{ nome: 'A', valor: (l) => l.a }, { nome: 'B', valor: (l) => l.b }];
async function escreve<T>(o: { arquivo: string; colunas: Coluna<T>[]; lotes: AsyncIterable<T[]> | Iterable<T[]> }): Promise<string> {
  const out: string[] = [o.arquivo];
  for await (const lote of o.lotes) for (const l of lote) out.push(o.colunas.map((c) => String(c.valor(l))).join(';'));
  return out.join('|');
}

const exato = (campo: string, valor: string | string[]) => {
  const tipo = Array.isArray(valor) ? 'terms' : 'term';
  return { bool: { should: [{ [tipo]: { [campo]: valor } }], minimum_should_match: 1 } };
};
const filtro = (de: string) => [{ term: { doc: 'd' } }, exato('fonte', 'fila'), { range: { dia: { gte: de } } }];
const fam = new Map<string, string[]>([['A', ['a1', 'a2']], ['B', ['b1']]]);
const porMae = Object.fromEntries([...fam].map(([mae, membros]) => [mae, exato('empresa', membros)]));
console.log(Object.keys(porMae).join(','), porMae['A'].bool.minimum_should_match);
const filter: Record<string, unknown>[] = [...filtro('1')];
filter.push({ exists: { field: 'x' } });
console.log(filter.length, JSON.stringify(filter[1]));

class Res { n = 0; write(s: string): boolean { this.n += s.length; return this.n < 3; } once(ev: string, f: () => void): Res { f(); return this; } }
const res = new Res();
function ok(): void { console.log('ok'); }
res.write('a') ? ok() : res.once('drain', () => ok());

interface Cand { cd: number; ok: boolean }
const cs: Cand[] = [{ cd: 1, ok: true }, { cd: 2, ok: false }, { cd: 1, ok: true }];
const ids = (f: (c: Cand) => boolean): number[] => [...new Set(cs.filter(f).map((c) => c.cd))];
console.log(ids((c) => c.ok), ids(() => true));

type Campo = 'a' | 'b';
const COLUNA: Record<Campo, string> = { a: 'COL_A', b: 'COL_B' };
const campos = Object.keys({ a: 1, b: 2 }) as Campo[];
const cols: string[] = campos.map((c) => COLUNA[c]);
console.log(cols.join(','));

const big = (v: number | bigint): bigint => BigInt(v);
console.log(String(big(5)), String(big(12n)));

function indexa(o: object): void { console.log(JSON.stringify(o)); }
function salva(id: string, versao?: { seq: number }): void {
  indexa({ id, ...(versao ? { if_seq_no: versao.seq } : { op_type: 'create' as const }) });
}
salva('1'); salva('2', { seq: 3 });

const REGRA = [20, 21, 22];
const passo = (de: number, ate: number): void => {
  for (let i = de; i < ate; i++) console.log(REGRA[i] != null ? REGRA[i] : 'fora');
};
passo(1, 4);

const banco = (d: string) => {
  const docs = [d];
  try {
    const oracle = new Map<string, number>([['a', d.length]]);
    if (d === 'x') throw new Error('boom');
    return { docs, oracle };
  } catch (err) {
    return { docs, oracle: null };
  }
};
console.log(banco('ab').oracle?.get('a'), banco('x').oracle === null);

interface In { sub: 'a' | 'b'; json?: string }
function ex(i: In): string {
  switch (i.sub) {
    case 'a':
      return 'A' + (i.json ?? '');
    case 'b':
      return 'B';
  }
}
console.log(ex({ sub: 'a', json: 'x' }), ex({ sub: 'b' }));

function notas(json: string): string {
  const parsed = JSON.parse(json) as { notifications?: unknown };
  const ns = Array.isArray(parsed?.notifications) ? parsed.notifications : [];
  if (ns.length >= 2) {
    const el: unknown = ns[1];
    if (typeof el === 'string') return el;
  }
  return '-';
}
console.log(notas('{"notifications":["a","b"]}'), notas('{"notifications":3}'), notas('{}'));

interface Payload { sub: string }
class Base {
  canActivate(n: number): boolean { return this.handleRequest(null, { sub: 'u' + n }) !== null; }
  handleRequest(err: Error | null, user: unknown): unknown { if (err) throw err; return user; }
}
class Guard extends Base {
  canActivate(n: number): boolean | Promise<boolean> {
    if (n === 0) return true;
    return super.canActivate(n);
  }
  handleRequest<TUser = Payload>(err: Error | null, user: unknown): TUser {
    const r = super.handleRequest(err, user) as TUser;
    console.log('handle', JSON.stringify(r));
    return r;
  }
}
const g: Base = new Guard();
console.log(g.canActivate(0), g.canActivate(2));
const direto = new Guard().canActivate(0);
console.log(typeof direto, direto);

class Mask {
  static corta(v: string): string { return v.slice(0, 2) + '***'; }
  private static readonly F: Record<string, (v: string) => string> = { cpf: (v: string) => Mask.corta(v), email: (v: string) => v.toUpperCase() };
  run(data: Record<string, unknown>): Record<string, unknown> {
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(data)) out[k] = typeof v === 'string' && Mask.F[k] ? Mask.F[k](v) : v;
    return out;
  }
}
console.log(JSON.stringify(new Mask().run({ cpf: '12345', email: 'a@b', n: 3 })));

interface Fam { mae: string; nomes?: Record<string, string> }
const fam1: Fam = { mae: 'M', nomes: { a: 'A' } };
const copia = { ...fam1 };
delete copia.nomes;
console.log(JSON.stringify(copia), JSON.stringify(fam1));

async function executa(n: number): Promise<number | null> { return n > 2 ? null : n * 10; }
async function main(): Promise<void> {
  const linhas: Linha[] = [{ a: 1, b: 'x' }, { a: 2, b: 'y' }];
  console.log(await escreve({ arquivo: 'f.csv', colunas: COLS, lotes: [linhas] }));
  const [persistido, ...runs] = await Promise.all([executa(0), ...[1, 2, 3].map((l) => executa(l))]);
  console.log(persistido, runs);
  const p = new Promise<void>((done) => (res.write('bcd') ? done() : res.once('drain', () => done())));
  await p;
  console.log('fim', res.n);
}
main();
