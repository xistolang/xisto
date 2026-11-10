// Port tail, round 8: forms a real NestJS service used (synthetic examples)
// build: --tsc-checked
import { Money } from './lib/money/index.ts';

// a static field of a class re-exported with export * (Class.ZERO through a barrel)
interface Acc { total: Money | null; v: Money | null }
const acc: Acc = { total: Money.ZERO, v: Money.of(2) };
console.log(acc.total?.plus(Money.of(3)).cents, acc.v?.cents);

// array destructuring whose initializer has a block-bodied arrow
type Item<T> = { ok: true; valor: T } | { ok: false; erro: unknown };
async function item<T>(fn: () => Promise<T>): Promise<Item<T>> {
  try { return { ok: true, valor: await fn() }; } catch (e) { return { ok: false, erro: e }; }
}
function valor<T>(it: Item<T>): T { if (!it.ok) throw it.erro; return it.valor; }
async function nome(): Promise<string> { return 'ana'; }
async function conta(): Promise<number> { return 3; }
function k(f: () => number): number { return f(); }
async function main(): Promise<void> {
  const [a, b] = await Promise.all([
    item(async () => nome()),
    item(async () => { const n = await conta(); return n + 1; }),
  ]);
  console.log(valor(a), valor(b));
  const [z] = [k(() => { return 7; })];
  console.log(z);
  // a plain arrow that gives a promise, where an async callback goes (generic): awaited
  const flag: boolean = z > 3;
  console.log(await run(() => (flag ? nome() : Promise.resolve('N'))), await run(() => Promise.resolve(5)));
  // string[] where (string | null)[] goes
  console.log(junta(['a', 'b']));
}
async function run<T>(fn: () => Promise<T>): Promise<T> { return await fn(); }
function junta(xs: (string | null)[]): string { return xs.map((x) => x ?? '-').join(','); }

// xs.map(f).filter(p) where f gives number | null
interface L { DT: string | null }
function ms(s: string | null): number | null { return s == null ? null : s.length * 1.5; }
const ls: L[] = [{ DT: 'abc' }, { DT: null }, { DT: 'a' }];
const xs = ls.map((l) => ms(l.DT)).filter((v): v is number => v !== null);
console.log(xs, xs.length);
const ys = [1, 2, 3, 4].map((n) => (n % 2 === 1 ? n * 0.5 : null)).filter((v): v is number => v != null);
console.log(ys);

// upcasts written with 'as', comparisons of a column read as string | number | null, endless loops that return
const o = { tratamento: 0 as number | null, nome: 'a' };
o.tratamento = null;
console.log(o.tratamento);
const n7: number = 7;
const u = n7 as number | string | null;
console.log(typeof u === 'number' ? u + 1 : -1);
interface R { CD: string | number | null }
const rows: R[] = [{ CD: 'a' }, { CD: 2 }, { CD: null }];
console.log(rows.filter((r) => r.CD === 'a').length, rows.filter((r) => r.CD === null).length);
function primeiroMaior(ns: number[]): number {
  let i = 0;
  for (;;) {
    if (i >= ns.length) return -1;
    if (ns[i] > 2) return ns[i];
    i++;
  }
}
console.log(primeiroMaior([1, 2, 3]), primeiroMaior([1]));
const pares: [string, number][] = [['a', 1], ['b', 2]];
const linhas = pares.map(([c]): [string, string] => [c, c.toUpperCase()]);
console.log(linhas);
main();
