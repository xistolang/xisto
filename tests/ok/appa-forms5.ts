// read as TypeScript reads them: a const its initializer's closure reads (it runs later), this.f instanceof C narrowing
// the field in the block, filter((o, i, xs): o is T => ...), Record<K, V>[k] with k a K, { ...rec, ...(c ? {..} : {}),
// k: v } of mixed values, { ...obj, ...(dto as any) }, Map<number | null, V>, a fresh object cast to a Record
// build: --tsc-checked
class Cache {
  estado: { valor: Promise<number> } | null = null;
  async mede(ok: boolean): Promise<number> {
    if (!ok) throw new Error('falhou');
    return 7;
  }
  le(ok: boolean): Promise<number> {
    const valor = this.mede(ok).catch((err: unknown) => {
      if (this.estado?.valor === valor) this.estado = null;
      throw err;
    });
    this.estado = { valor };
    return valor;
  }
}
interface Coletor { fim(): void }
class Granular implements Coletor {
  n = 0;
  fim(): void { this.n++; }
  async flush(): Promise<number> { return this.n + 10; }
}
class Outro implements Coletor { fim(): void {} }
class Ctl {
  private readonly c: Coletor;
  constructor(c: Coletor) { this.c = c; }
  async roda(): Promise<number> {
    this.c.fim();
    if (this.c instanceof Granular) {
      return await this.c.flush();
    }
    return -1;
  }
}
const texto = (v: unknown): string | null => (typeof v === 'string' && v ? v : null);
type Campo = 'dtA' | 'nome';
const COLUNA: Record<Campo, string> = { dtA: 'DT_A', nome: 'NM' };
function rid(h: string | null): Record<string, string> { return h ? { 'http.request.id': h } : {}; }
function log(o: Record<string, unknown>): string { return JSON.stringify(o); }
interface Dto { cdRegra: number; nome: string }
function usa(d: Dto): string { return `${d.cdRegra}:${d.nome}`; }
function atualiza(dto: Partial<Dto>): string {
  const base: Dto = { cdRegra: 5, nome: 'velho' };
  const eff = { ...base, ...(dto as any) };
  return usa(eff);
}
const seq = new Map<number | null, number>();
function proximo(id: number | null): number {
  const n = (seq.get(id) ?? 0) + 1;
  seq.set(id, n);
  return n;
}
class Ent { cd: number = 1; nome: string = 'a'; }
function chaves(r: Record<string, any>): string { return Object.keys(r).join(','); }
async function main(): Promise<void> {
  const c = new Cache();
  console.log(await c.le(true), c.estado !== null);
  try { await c.le(false); } catch (e) { console.log((e as Error).message, c.estado === null); }
  console.log(await new Ctl(new Granular()).roda(), await new Ctl(new Outro()).roda());
  const ops: string[] = [texto('a'), texto(null), texto('a'), texto('b')].filter((o, i, xs): o is string => !!o && xs.indexOf(o) === i);
  console.log(ops.join(','));
  const campos: Campo[] = ['nome', 'dtA'];
  console.log(campos.map((k) => COLUNA[k]).join(','));
  const u: { usuario?: string } | null = { usuario: 'ana' };
  console.log(log({ 'http.status': 200, 'url.path': '/x', ...(u?.usuario ? { 'user.name': u.usuario } : {}), ...rid('r1'), ...rid(null) }));
  console.log(atualiza({ nome: 'novo' }), atualiza({}), atualiza({ cdRegra: 9 }));
  console.log(proximo(1), proximo(1), proximo(null), proximo(null), proximo(2), seq.has(1.0), seq.has(3), seq.size);
  console.log(chaves(new Ent() as unknown as Record<string, any>));
}
main();
