// build: --tsc-checked
// warns: XS2107
// Round-5 port forms: deep path narrowing, A & { f: T } narrower fields, T | null where T goes (checked),
// identity compare of nullable references and kept promises, never-null tests on pure reads,
// an object literal into Record<string, unknown> | unknown[].

interface Base { id: number; name?: string | null }
interface Ctx { aut: Base & { name: string; extra?: number } ; tag: string | null }

function takeStr(s: string): number { return s.length; }
function takeNum(n: number): number { return n * 2; }

function deep(ctx: Ctx): string {
  let out = '';
  if (ctx.aut.extra != null) out += 'extra=' + takeNum(ctx.aut.extra) + ';';
  out += 'name=' + takeStr(ctx.aut.name) + ';';
  return out;
}

class Node1 { v: number; constructor(v: number) { this.v = v; } }
function sameNode(m: Map<string, Node1>, k: string, n: Node1): boolean { return m.get(k) === n; }

async function kept(): Promise<string> {
  const m = new Map<string, Promise<string>>();
  const p: Promise<string> = (async () => 'seven')();
  m.set('a', p);
  const v = await p;
  return (m.get('a') === p ? 'same' : 'other') + ' ' + v;
}

interface Row { VL: string }
function neverNull(r: Row): string {
  if (r.VL === null || r.VL === undefined) return 'none';
  return r.VL;
}

type Binds = Record<string, unknown> | unknown[];
function q(sql: string, binds: Binds): string {
  return Array.isArray(binds) ? sql + ' [' + binds.length + ']' : sql + ' {' + Object.keys(binds).join(',') + '}';
}

const ctx: Ctx = { aut: { id: 1, name: 'abc', extra: 4 }, tag: null };
console.log(deep(ctx));
console.log(deep({ aut: { id: 2, name: 'xy' }, tag: 'k' }));
const n = new Node1(3);
const m = new Map<string, Node1>([['a', n]]);
console.log(sameNode(m, 'a', n), sameNode(m, 'b', n), sameNode(m, 'a', new Node1(3)));
console.log(neverNull({ VL: 'x' }));
console.log(q('s1', { a: 1, b: 'two' }), q('s2', [1, 2, 3]), q('s3', {}));
kept().then((s) => console.log(s));
function viaRecord(r: Record<string, string>, k: string): number { return takeStr(r[k]); }
const rr: Record<string, string> = { a: 'four' };
console.log(viaRecord(rr, 'a'));
try { viaRecord(rr, 'zz'); } catch (e) { console.log('missing key:', e instanceof Error); }
function summarize(v: unknown): string {
  if (Array.isArray(v)) return 'array of ' + v.length + ' first ' + JSON.stringify(v.slice(0, 1));
  return 'other';
}
console.log(summarize([1, 2, 3]), summarize('x'));
function tokenName(t: unknown): string | null {
  if (typeof t === 'symbol') return t.description ?? null;
  return null;
}
console.log(tokenName(Symbol('TOKEN')), tokenName('s'));
function rowsOr(x: any): number { return (x ?? []).length; }
console.log(rowsOr(null), rowsOr([1, 2]));
const audit = { por: 'ana', detalhe: null, n: 1 };
console.log(JSON.stringify(audit));
async function nada(): Promise<void> { console.log('nada ran'); }
async function protege<T>(fn: () => Promise<T>): Promise<T> { return await fn(); }
protege(() => nada()).then(() => console.log('protected'));
const counts: Record<string, number> = { a: 0 };
counts.a++;
counts['a']++;
console.log(JSON.stringify(counts));
console.log(Array.from({ length: 3 }, (_, i) => `:s${i}`).join(', '));
let calls = 0;
const worker = (): number => ++calls;
console.log(Array.from({ length: 2 }, worker), Array.from([1, 2, 3], (x) => x * 10));
function idString(id: number): string { return typeof id === 'object' && id !== null ? Object.values(id).join('/') : String(id); }
console.log(idString(42));
const order = (a: number, b: number): number => a - b;
interface NS { n: number; s: string }
const byName = (x: NS, y: NS): number => order(x.n, y.n) || x.s.localeCompare(y.s);
const nss: NS[] = [{ n: 1, s: 'b' }, { n: 1, s: 'a' }, { n: 0, s: 'z' }];
console.log(nss.sort(byName).map((v) => v.s).join(''));
const pct = 12.345;
console.log(+pct.toFixed(2), [...new Set([3, 1, 3, 2])].join(','));
function when(v: Date | string): string { return new Date(v).toISOString(); }
console.log(when('2024-01-02T03:04:05Z'), when(new Date(0)));
