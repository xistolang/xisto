// appa-types-4: string +, number keys on Record<number, V>, structural copies, ?: unions, Dates compared,
// Object.fromEntries, casts of errors and Records through shapes
// warns: XS2106 6
const n: int = 3;
const x = 2.5;
const s: string | null = null;
const k: number | null = 4;
const b: boolean | null = true;
console.log("n=" + n, "x=" + x, "s=" + s, "k=" + k, "b=" + b, x + "!");

const ROT: Record<number, string> = { 1: 'um', 2: 'dois' };
const rot = (v: number): string => ROT[v] ?? `status ${v}`;
console.log(rot(1), rot(3), rot(1.5));

interface Pedido { id: number; nome: string; extra: string | null }
class Linha { id = 0; nome = ''; extra: string | null = null; total = 1.5; dobro(): number { return this.total * 2; } }
function usa(p: { id: number; nome: string }): string { return `${p.id}:${p.nome}`; }
function usaN(p: { id: number; nome: string } | null): string { return p === null ? 'nada' : `${p.id}/${p.nome}`; }
function binds(r: Record<string, unknown>): string { return JSON.stringify(r); }
const l = new Linha();
l.id = 7;
l.nome = 'sete';
const pe: Pedido = { id: 3, nome: 'tres', extra: null };
console.log(usa(l), usa(pe), usaN(null), usaN(pe), l.dobro());
const bb = { dest: 'x', n: 2, z: null as string | null };
console.log(binds(bb), binds(pe));

function pick(c: boolean): string | number { return c ? 'a' : 1; }
console.log(pick(true), pick(false));
const flag = n > 2;
const lista = flag ? [`tem ${n}`] : [];
const vazia = !flag ? [] : [1, 2];
console.log(lista, vazia);

const d1 = new Date(Date.UTC(2024, 0, 1));
const d2 = new Date(Date.UTC(2024, 5, 1));
console.log(d1 < d2, d1 >= d2, d2 > d1);

const pares: [string, number][] = [['a', 1], ['b', 2]];
const rec = Object.fromEntries(pares);
console.log(rec, rec['b']);
const rec2: Record<string, string> = Object.fromEntries(pares.map((p) => [p[0], `v${p[1]}`] as [string, string]));
console.log(rec2);

class OraErr extends Error { errorNum = 942; }
function num(e: Error): number | null { return e instanceof OraErr ? e.errorNum : null; }
console.log(num(new OraErr('x')), num(new Error('y')));
const solto: Record<string, unknown> = { a: 1, b: 'dois', c: null };
const tipado = solto as Record<string, string | number | null>;
console.log(tipado['a'], tipado['b'], tipado['c']);

function rows(m: number): Record<string, unknown>[] | null { return m > 0 ? [{ a: m }] : null; }
function listaDe(m: number): Record<string, unknown>[] { const r = rows(m); return Array.isArray(r) ? r : []; }
console.log(listaDe(2), listaDe(0));
function big(v: number | bigint): string { return typeof v === 'number' ? `n${v}` : `b${v}`; }
console.log(big(n), big(2.5));
const raw: object = JSON.parse('[{"a":1,"b":"x","c":[1,{"d":true}]},{"a":null}]') as object;
const rs = raw as Record<string, unknown>[];
console.log(rs.length, rs[0]['b'], rs[1]['a'], JSON.stringify(rs[0]['c']));

function igual(a: number | null, c: number | null): boolean { return a == null || c == null ? a === c : Math.abs(a - c) < 1e-6; }
console.log(igual(null, null), igual(1, null), igual(2, 2), igual(2, 3));
const t1: string | null = 'a', t2: string | null = null;
console.log(t1 === t2, t1 !== t2, t2 === null);
const id = 7;
const params = n > 2 ? [id, 'ativo', 10, 0] : [id, 'x'];
console.log(params.length, params[1]);

const base: Record<string, unknown> = { a: 1, b: 'x' };
const extra: Record<string, unknown> = { b: 'y', c: true };
const junto: Record<string, unknown> = { ...base, ...extra, d: null };
console.log(junto, Object.keys(junto).length);
const nums: Record<string, number> = { p: 1 };
const copia = { ...nums };
copia['q'] = 2;
console.log(nums, copia);
