// forms of the kiosk (round 5): () => undefined, Array.isArray on string | string[] | undefined, Record<string, V> with number keys,
// if (rows[0]) past the end, k in o with a key known at run time (and in an Error), Object.assign(target, { [k]: v })
async function falha(): Promise<number> { throw new Error('x'); }
async function main_t36(): Promise<void> {
  const p = falha();
  void p.catch(() => undefined);
  const r = await p.catch(() => null);
  console.log(r === null);
}

function ip(xff: string | string[] | undefined): string {
  return (Array.isArray(xff) ? xff[0] : xff)?.split(',')[0]?.trim() ?? 'nenhum';
}
console.log(ip('1.2.3.4, 5.6.7.8'), ip(['9.9.9.9', 'x']), ip(undefined));
const binds: Record<string, unknown> = {};
for (let i = 1; i <= 3; i++) binds[i] = 'v' + i;
binds[45] = 7;
const n = 2.5;
binds[n] = 'meio';
console.log(JSON.stringify(binds), binds[2], binds['45']);
interface Row { ID: number }
function primeiro(rows: Row[]): string {
  return rows[0] ? 'id ' + rows[0].ID : 'vazio';
}
function nada(us: string[]): string | null { if (!us[0]) return null; return us[0]; }
console.log(primeiro([{ ID: 3 }]), primeiro([]), nada([]), nada(['a']), nada(['']));
const COLS: { nome: string; cd: string } = { nome: 'NM', cd: 'CD' };
function tem(k: string): boolean { return Object.prototype.hasOwnProperty.call(COLS, k); }
function link(body: unknown): string {
  if (typeof body !== 'object' || body === null || !('link' in body)) return 'sem link';
  return 'com link';
}
class Erro extends Error { causa: string = 'x'; }
const e: Error = new Erro('m');
console.log(tem('nome'), tem('outro'), link({ link: 1 }), link({ a: 1 }), link(null), 'cause' in e, 'cause' in new Error('x', { cause: 1 }));
interface Grupo { nome?: string; nivel?: number; ativo?: boolean }
const dto: Grupo = { nome: 'g', ativo: false };
const patch: Partial<Grupo> = {};
for (const field of ['nome', 'nivel', 'ativo'] as const) {
  if (dto[field] !== undefined) Object.assign(patch, { [field]: dto[field] });
}
const out: Record<string, unknown> = {};
Object.assign(out, { x: 1, ['y']: 'b' });
console.log(JSON.stringify(patch), JSON.stringify(out));
main_t36();
