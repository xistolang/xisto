// read as TypeScript reads them: xs[i] != null on a number[] (past its end), c ? { a: x } : { a: null } (one type, a | null),
// c ? await f() : new Map(), v.toString() of a narrowed unknown, (v ?? null) === (o ?? null) of unknowns, d[k]++ with k a
// union of d's field names, a function (r, o: Partial<Omit<T, 'k'>> = {}) where (r, o?: Partial<T>) goes (it only reads o)
// build: --tsc-checked
const REGRA: number[] = [10, 20];
for (let i = 0; i < 4; i++) console.log(i, REGRA[i] != null, REGRA[i] == null);
interface Ctl { cd: string; tab: number }
function seed(c: Ctl | null): string {
  const s = c ? { cd: c.cd, tab: c.tab } : { cd: null, tab: 0 };
  return `${s.cd ?? '-'}:${s.tab}`;
}
console.log(seed({ cd: 'E1', tab: 3 }), seed(null));
async function nomes(ps: number[]): Promise<Map<number, string>> { return new Map(ps.map((p) => [p, 'n' + p] as [number, string])); }
async function f(proc: number | null): Promise<string | null> {
  const m = proc ? await nomes([proc]) : new Map();
  return proc != null ? (m.get(proc) ?? null) : null;
}
async function mainNm(): Promise<void> { console.log(await f(3), await f(null)); }
const comoTexto2 = (v: unknown): string =>
typeof v === 'string' ? v : typeof v === 'number' || typeof v === 'bigint' ? v.toString() : JSON.stringify(v);
console.log(comoTexto2('a'), comoTexto2(3.5), comoTexto2({ x: 1 }));
const calc2: Record<string, unknown> = { a: 1, b: 'x', c: null };
const v1b: Record<string, unknown> = { a: 1, b: 'y', c: null };
const outB: string[] = [];
for (const [k, v] of Object.entries(calc2)) {
  const o = v1b[k] ?? null;
  const igual = typeof v === 'number' || typeof o === 'number' ? v !== null && o !== null && Number(v) === Number(o) : (v ?? null) === (o ?? null);
  if (!igual) outB.push(k);
}
console.log(outB.join(','));
type Motivo = 'semLinha' | 'erro';
interface Descartes { total: number; semLinha: number; erro: number }
const CONT: Record<Motivo, 'semLinha' | 'erro'> = { semLinha: 'semLinha', erro: 'erro' };
function conta(d: Descartes, m: Motivo): void { d.total++; d[CONT[m]]++; }
const dd: Descartes = { total: 0, semLinha: 0, erro: 0 };
conta(dd, 'erro'); conta(dd, 'erro'); conta(dd, 'semLinha');
console.log(JSON.stringify(dd));
interface Item { empresa: string; mae: string | null; codigo: string | null }
function valida(item: (r: string, o?: Partial<Item>) => Item): Item {
  const a = item('OK', { mae: 'M', codigo: 'C9' });
  const b = item('SEM');
  return { empresa: a.empresa + '/' + b.empresa, mae: a.mae, codigo: b.codigo };
}
function principal(empresa: string): string {
  const item = (r: string, o: Partial<Omit<Item, 'empresa'>> = {}): Item => ({ empresa: `${empresa}:${r}`, mae: o.mae ?? null, codigo: o.codigo ?? null });
  const x = valida(item);
  return `${x.empresa} ${x.mae} ${x.codigo}`;
}
console.log(principal('E1'));
mainNm();
