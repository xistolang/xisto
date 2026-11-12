// binds as unknown as unknown[] with binds a Record reaches a Record | unknown[] parameter as the Record it is (XB-182);
// Map<object, V> and WeakMap<K, V> keyed by identity; for (const { name, ...body } of xs)
// build: --tsc-checked
function q(sql: string, params: Record<string, unknown> | unknown[] = []): string {
  if (Array.isArray(params)) return `${sql} pos:${params.length}`;
  return `${sql} nomes:${Object.keys(params).join(',')}`;
}
type Binds = Record<string, unknown>;
function consulta(sql: string, binds: Binds): string {
  return q(sql, binds as unknown as unknown[]);
}
class Slot<T> {
  private readonly loja: Map<object, unknown>;
  constructor(loja: Map<object, unknown>) { this.loja = loja; }
  ler(): T | null { return (this.loja.get(this) as T | undefined) ?? null; }
  gravar(v: T | null): void { if (v === null) this.loja.delete(this); else this.loja.set(this, v); }
}
interface Ctx { empresa: string }
const memo = new WeakMap<Ctx, Map<string, number>>();
function porEmpresa(ctx: Ctx): Map<string, number> {
  const pronto = memo.get(ctx);
  if (pronto) return pronto;
  const m = new Map<string, number>([[ctx.empresa, ctx.empresa.length]]);
  memo.set(ctx, m);
  return m;
}
function tpls(): Array<{ name: string; index_patterns: string[]; priority: number }> {
  return [{ name: 'a', index_patterns: ['x-*'], priority: 1 }, { name: 'b', index_patterns: [], priority: 2 }];
}
console.log(consulta('a', { x: 1, y: 'z' }), q('b', [1, 2]));
const loja = new Map<object, unknown>();
const a = new Slot<string>(loja);
const b = new Slot<string>(loja);
a.gravar('x');
console.log(a.ler(), b.ler(), loja.size, loja.has(a));
a.gravar(null);
console.log(a.ler(), loja.size);
const c1: Ctx = { empresa: 'abc' };
const c2: Ctx = { empresa: 'abc' };
console.log(porEmpresa(c1) === porEmpresa(c1), porEmpresa(c1) === porEmpresa(c2), memo.has(c2));
const templates = tpls();
for (const { name, ...body } of templates) console.log(name, JSON.stringify(body));
