// read as TypeScript reads them: [] with nothing to type it (never[]) where a T[] goes, p.catch(() => []), Map and Set
// forEach, (obj as unknown as Record<string, unknown>)[k] = v on an object of a class, typeof args[0] === 'string'
// narrowing args[0], ...new Set(ints) where number[] goes
// build: --tsc-checked
interface Pag { total: number; items: string[] }
function pagina(ok: boolean): Pag {
  const vazio = { total: 0, items: [] };
  return ok ? { total: 1, items: ['a'] } : vazio;
}
async function ultimos(falha: boolean): Promise<number[]> {
  if (falha) throw new Error('x');
  return [1, 2];
}
async function lidos(falha: boolean): Promise<number[]> {
  const xs = await ultimos(falha).catch(() => []);
  return xs;
}
class Res { total: number = 0; nome: string = 'a'; }
const patched = async function (this: unknown, ...args: unknown[]): Promise<string> {
  const sql = typeof args[0] === 'string' ? args[0] : '';
  return sql.toUpperCase();
};
async function main(): Promise<void> {
  console.log(JSON.stringify(pagina(true)), JSON.stringify(pagina(false)), (await lidos(false)).join(','), (await lidos(true)).length);
  const h = new Map<string, string>([['a', '1'], ['b', '2']]);
  const out: string[] = [];
  h.forEach((v, k) => { out.push(`${k}=${v}`); });
  new Set<number>([3, 4]).forEach((x) => out.push(String(x)));
  console.log(out.join(','));
  const r = new Res();
  const k: string = 'total';
  (r as unknown as Record<string, unknown>)[k] = 5;
  (r as unknown as Record<string, unknown>)['nome'] = 'b';
  console.log(r.total, r.nome);
  console.log(await patched('select 1'), JSON.stringify(await patched(7)));
  const ids = new Map<number, string>([[3, 'a'], [4, 'b']]);
  const xs: number[] = [1.5, ...new Set([...ids.keys()].filter((n) => n > 3))];
  console.log(xs.join(','));
}
main();
