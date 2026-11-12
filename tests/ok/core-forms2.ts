// forms of core and kiosk (round 6b): typeof of a const made by a generic call (registerAs), a class of fields returned
// where another goes, includes of objects, Object.entries of object, { [k]: v } where object goes, await p.catch(log),
// () => void where () => unknown goes, return Promise.reject(e), Number(s || 0), unknown[] filtered by a predicate or
// spread into string[], error.code
// build: --tsc-checked
import { dbs } from './lib/cfgtypeof/cfg.ts';
class Svc { f: typeof dbs; constructor(f: typeof dbs) { this.f = f; } first(): string { return this.f()[0].name; } }
console.log(new Svc(dbs).first());
class Ident {
  sub = "";
  email?: string;
  linkedAt = new Date(0);
  createdAt?: Date;
}
class IdentDto {
  sub = "";
  email?: string;
  linkedAt = new Date(0);
}
async function getIdentity(s: string): Promise<Ident> {
  const i = new Ident();
  i.sub = s; i.linkedAt = new Date(0);
  return i;
}
async function get(s: string): Promise<IdentDto> {
  return getIdentity(s);
}
async function get2(s: string): Promise<IdentDto> {
  const x = await getIdentity(s);
  return x;
}
async function main1(): Promise<void> {
  const a = await get('k1'), b = await get2('k2');
  console.log(a.sub, a.linkedAt.toISOString(), b.sub, b.email ?? '-');
}
class Col { name: string; constructor(n: string) { this.name = n; } }
const a = new Col('a'), b = new Col('b'), c = new Col('c');
const dates = [a, c];
console.log([a, b, c].map((x) => dates.includes(x)).join(','), dates.indexOf(c), dates.indexOf(b));
function compact(patch: object, omit: readonly string[] = ['id']): Record<string, unknown> {
  const skip = new Set(omit);
  const out: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(patch)) {
    if (skip.has(key) || value === undefined) continue;
    out[key] = value;
  }
  return out;
}
interface Patch { id?: number; nome?: string; ativo?: boolean; }
const p: Patch = { id: 1, nome: 'x', ativo: true };
console.log(JSON.stringify(compact(p)));
interface FindOpts { order?: object; skip?: number; }
function find(o: FindOpts): string { return JSON.stringify(o.order); }
const sortCol = 'cdX';
const dir: 'ASC' | 'DESC' | undefined = undefined;
console.log(find({ order: { [sortCol]: dir ?? 'ASC' }, skip: 0 }));
async function exec(sql: string): Promise<unknown[]> { throw new Error('boom ' + sql); }
async function main2(): Promise<void> {
  await exec('u').catch((err) => console.log('logged', (err as Error).message));
  console.log('after');
}
async function logar(msg: string): Promise<void> { console.log('log', msg); }
function seguro(fn: () => unknown): void { fn(); }
seguro(() => console.log('void arrow'));
interface Ben { nome: string; }
async function carregar(ok: boolean): Promise<Ben> {
  if (!ok) return Promise.reject(new Error('NAO_ENCONTRADO'));
  return { nome: 'b' };
}
function nr(senha: string): number { return Number(senha || 0); }
async function main3(): Promise<void> {
  console.log((await carregar(true)).nome);
  try { await carregar(false); } catch (e) { console.log('rejeitou', (e as Error).message); }
  console.log(nr(''), nr('12'), nr('x'));
  await logar('fim');
}
await main1();
await main2();
await main3();
const raw: unknown[] = ['b', 3, 'a', '', 'c'];
const ents = raw.filter((x): x is string => typeof x === 'string' && x !== '').sort();
console.log(ents.join(','));
const more: unknown[] = ['z', 'a'];
console.log([...new Set<string>([...['q'], ...more])].sort().join(','));
try { JSON.parse('{'); } catch (error) {
  const err = error as Error;
  console.log(err?.code ?? 'sem code');
}
const series = Array.from({ length: 3 }, (_, i) => ({ day: i, count: i * 2 }));
console.log(series.reduce((sum, d) => sum + d.count, 0));
