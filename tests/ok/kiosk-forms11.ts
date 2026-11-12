// forms of the kiosk (round 9): new Array<R>(n) set later, const x = null, (d): d is NonNullable<typeof d>, an array of
// objects of unrelated classes, (e as { cause?: unknown } | null)?.cause on an Error, (s as { valueOf(): string }).valueOf() (d, i, arr) callbacks, { ...entity, ...Object.fromEntries(pairs) }
// build: --tsc-checked
async function mapComLimite<T, R>(itens: readonly T[], fn: (item: T, i: number) => Promise<R>): Promise<R[]> {
  const resultado = new Array<R>(itens.length);
  for (let i = itens.length - 1; i >= 0; i--) resultado[i] = await fn(itens[i], i);
  return resultado;
}
async function main(): Promise<void> {
  const r = await mapComLimite(['a', 'b'], async (s, i) => s + i);
  const n = await mapComLimite([1, 2, 3], async (x) => x * 2);
  console.log(r.join(','), n.join(','));
  const cdFil = null;
  const o: { cdFil: string | null } = { cdFil };
  console.log(o.cdFil === null);
}
await main();
const xs: ({ a: number } | null)[] = [{ a: 1 }, null, { a: 3 }];
const ys = xs.map((x) => x).filter((d): d is NonNullable<typeof d> => !!d);
console.log(ys.map((y) => y.a).join(','));
class A9 { a = 1; }
class B9 { b = 'x'; }
function n(xs: unknown[]): number { return xs.length; }
const alvos = [new A9(), new B9()];
console.log(n(alvos));
function dsJson(erro: unknown): string {
  const e = erro instanceof Error ? erro : null;
  const cause = (e as { cause?: unknown } | null)?.cause;
  return cause instanceof Error ? 'cause ' + cause.message : 'no cause';
}
console.log(dsJson(new Error('a', { cause: new Error('root') })), dsJson(new Error('b')), dsJson('x'));
function norm(d: string | null): string {
  const ehN = (d as { valueOf(): string }).valueOf() === 'N';
  return ehN ? 'D13' : (d as string);
}
console.log(norm('N'), norm('X'));
try { norm(null); } catch (e) { console.log((e as Error).name); }
const ds = [{ c: 2 }, { c: 1 }, { c: 2 }, { c: 3 }]
  .map((d) => ({ c: d.c * 1 }))
  .filter((d, i, arr) => arr.findIndex((x) => x.c === d.c) === i);
console.log(ds.map((d) => d.c).join(','));
console.log([5, 6, 5].filter((x, i, a) => a.indexOf(x) === i).join(','));
class Ent2 { cd: number = 0; dt: Date | null = null; nome: string = ''; }
function restore<T extends object>(entities: T[], fields: string[], raw: Record<string, unknown>): T[] {
  return entities.map((entity) => {
    return raw ? { ...entity, ...Object.fromEntries(fields.map((field) => [field, raw[field]])) } : entity;
  });
}
const e = new Ent2(); e.cd = 1; e.nome = 'a';
const r = restore([e], ['dt'], { dt: new Date(0) });
console.log(r[0].cd, r[0].nome, r[0].dt instanceof Date, r[0].dt?.toISOString(), e.dt);
