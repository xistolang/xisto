// read as TypeScript reads them: let x = null typed by its assignments, c ? call() : Promise.resolve({...} / []), a ?: of
// two void calls, Pick<ReturnType<typeof cfg>, 'k'>, T['k'] of an interface's base, s ?? [] with a Set or Map,
// find on numbers, typeof rec === 'object' && rec !== null with rec a Record
// build: --tsc-checked
async function ler(ok: boolean): Promise<{ dias: number }> { if (!ok) throw new Error('x'); return { dias: 3 }; }
async function sombra(ok: boolean, tenta: boolean): Promise<string> {
  let s = null;
  if (tenta) {
    try { s = await ler(ok); } catch (err) { console.log('falhou'); }
  }
  return s === null ? 'nada' : String(s.dias);
}
class Repo {
  async contagem(): Promise<Record<string, number>> { return { A: 2, B: 3 }; }
  async ultimos(): Promise<string[]> { return ['p1']; }
}
async function laco(): Promise<void> { console.log('laco'); }
async function comLog(n: string, f: () => Promise<void>): Promise<void> { console.log('com', n); await f(); }
export const repescaConfig = () => ({ horaUtc: 5, diasDe: 3 });
export type RepescaConfig = ReturnType<typeof repescaConfig>;
function proxima(cfg: Pick<RepescaConfig, 'horaUtc'>): number { return cfg.horaUtc * 2; }
interface Doc { cd: string; elegibilidade: { falhas: string[] } | null }
interface Emp extends Doc { nome: string }
const falhasDe = (e: Emp['elegibilidade']): string[] => e?.falhas ?? [];
function itera(s: Set<number> | null, m: Map<string, number> | null): string {
  const out: string[] = [];
  for (const x of s ?? []) out.push(String(x));
  for (const [k, v] of m ?? []) out.push(`${k}${v}`);
  return out.join(',');
}
const TAM = [10, 50, 100];
function lote(q: number): number { return TAM.find((t) => q <= t) ?? 1000; }
function idString(id: Record<string, unknown>): string {
  return typeof id === 'object' && id !== null ? Object.values(id).join('/') : String(id);
}
async function main(): Promise<void> {
  console.log(await sombra(true, true), await sombra(false, true), await sombra(true, false));
  const r = new Repo();
  for (const fase of [1, 2]) {
    const [cont, ped] = await Promise.all([
      fase === 2 ? r.contagem() : Promise.resolve({ A: 0, B: 0 }),
      fase === 2 ? r.ultimos().catch(() => []) : Promise.resolve([]),
    ]);
    console.log(JSON.stringify(cont), ped.length);
  }
  for (const c of ['x', null]) await (c ? comLog(c, laco) : laco());
  console.log(proxima({ horaUtc: 4 }), proxima(repescaConfig()));
  console.log(falhasDe({ falhas: ['a', 'b'] }).join(','), falhasDe(null).length);
  console.log(itera(new Set([1, 2]), new Map([['a', 1]])), JSON.stringify(itera(null, null)));
  console.log(lote(7), lote(60), lote(5000), idString({ a: 1, b: 'x' }));
}
main();
