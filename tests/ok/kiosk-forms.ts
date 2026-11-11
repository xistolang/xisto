// forms of a NestJS port (kiosk): let x; typed by its first assignment, let x: T['k'];, types declared in a function,
// undefined as a type, typeof a.b in a type, a parameter defaulted to a const, fields typed by a call or an arrow,
// f?.(x) and Promise<void> | void (last: its async main prints after the rest, in Node and here)
interface Dedo { ind: number; imagem?: { tp: string; res: number } }
function parse(xs: string[]): Dedo[] {
  const out: Dedo[] = [];
  for (const x of xs) {
    let imagem: Dedo['imagem'];
    if (x.length > 2) imagem = { tp: x, res: x.length };
    out.push(imagem ? { ind: 1, imagem } : { ind: 2 });
  }
  return out;
}
console.log(JSON.stringify(parse(['ab', 'abcd'])));
function verify(token: string): { sub: string; n: number } {
  if (token === 'bad') throw new Error('bad token');
  return { sub: token.toUpperCase(), n: token.length };
}
function qr(token: string): string {
  let payload;
  try {
    payload = verify(token);
  } catch {
    return 'invalid';
  }
  return payload.sub + ':' + payload.n;
}
console.log(qr('abc'), qr('bad'));
let total, nome;
total = 3;
nome = 'x';
total += 2;
console.log(total, nome);
function grupos(xs: number[]): string {
  type Local = { id: number; nome: string };
  interface Par { a: Local; b: Local | null }
  const ls: Local[] = xs.map((x) => ({ id: x, nome: 'n' + x }));
  const p: Par = { a: ls[0], b: ls.length > 1 ? ls[1] : null };
  return JSON.stringify(p);
}
function outro(): string {
  type Local = { k: string };
  const l: Local = { k: 'v' };
  return l.k;
}
console.log(grupos([1, 2]), grupos([3]), outro());
class SemQuery {
  transform(value: unknown): undefined {
    const chaves = value && typeof value === 'object' ? Object.keys(value) : [];
    if (chaves.length > 0) throw new Error(chaves.map((c) => `property ${c} should not exist`).join(', '));
    return undefined;
  }
}
function padrao(n: number, _q?: undefined): string { return 'p' + n + ':' + (_q === undefined); }
const s = new SemQuery();
console.log(s.transform(null) === undefined, s.transform({}) == null, padrao(1));
try { s.transform({ rede: 'x' }); } catch (e) { console.log((e as Error).message); }
class Exame { cd: number = 0; nome: string = ''; }
class Dto { exames: Exame[] = []; nu: number = 0; }
function primeiro(dto: Dto): string {
  const lista: { exame: (typeof dto.exames)[number]; ordem: number }[] = dto.exames.map((e, i) => ({ exame: e, ordem: i }));
  const n: typeof dto.nu = dto.nu + 1;
  return lista.map((x) => x.exame.nome + '#' + x.ordem).join(',') + ' ' + n;
}
const d = new Dto();
const a = new Exame(); a.cd = 1; a.nome = 'hemo';
const b = new Exame(); b.cd = 2; b.nome = 'raio';
d.exames = [a, b];
console.log(primeiro(d));
const FETCH_TIMEOUT_MS = 15000;
const PREFIXO = 'url:';
function fetchBuffer(url: string, timeoutMs = FETCH_TIMEOUT_MS, pre = PREFIXO): string {
  return pre + url + ' em ' + (timeoutMs / 1000) + 's';
}
console.log(fetchBuffer('a'), fetchBuffer('b', 2500), fetchBuffer('c', 1, 'x:'));
function flagLigada(v: string | undefined): boolean { return v === 'true' || v === '1'; }
function sqlMarcada(banco: string): string { return `SELECT 1 FROM t@${banco}`; }
class Banco {
  consultas = 0;
  query(sql: string, binds: unknown[]): Promise<Record<string, unknown>[]> {
    this.consultas++;
    return Promise.resolve([{ SQL: sql, N: binds.length }]);
  }
}
class Servico {
  private readonly travaHabilitada = flagLigada(process.env.TRAVA_TESTE_XYZ);
  readonly SQL_MARCADA = sqlMarcada('hap');
  private readonly q = (sql: string, binds: unknown[]): Promise<Record<string, unknown>[]> =>
    this.banco.query(sql, binds);
  private readonly banco: Banco;
  constructor(banco: Banco) { this.banco = banco; }
  async rodar(): Promise<string> {
    const rows = await this.q(this.SQL_MARCADA, [1, 2]);
    return `${this.travaHabilitada} ${rows[0].SQL} ${rows[0].N} ${this.banco.consultas}`;
  }
}
type OnErr = (erro: unknown) => Promise<void> | void;
const log: string[] = [];
async function salvar(ok: boolean, onException?: OnErr): Promise<string> {
  try {
    if (!ok) throw new Error('falhou');
    return 'salvo';
  } catch (erro) {
    await onException?.(erro);
    return 'erro';
  }
}
function fmt(f: ((n: number) => string) | null, n: number): string {
  return f?.(n) ?? 'sem f';
}
async function main(): Promise<void> {
  console.log(await new Servico(new Banco()).rodar());
  console.log(await salvar(true), await salvar(false));
  console.log(await salvar(false, async (e) => { log.push('async:' + (e as Error).message); }));
  console.log(await salvar(false, (e) => { log.push('sync:' + (e as Error).message); }));
  console.log(log.join(' | '), fmt((n) => 'n=' + n, 3), fmt(null, 4));
}
main();
