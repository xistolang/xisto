// forms of the kiosk: a field typed by a call of a const arrow declared above, encodeURIComponent & co, Boolean(x),
// an interface's generic method query<T = D>(sql) called as ds.query<Row>(sql) and served by a class's query<T>
const flagLigada = (valor: string | undefined): boolean => valor !== 'false' && valor !== '0';
const sqlMarcada = (banco: string): string => `SELECT 1 FROM t@${banco}`;
class Trava {
  private readonly habilitada = flagLigada(process.env.TRAVA_TESTE_NAO_DEFINIDA);
  readonly SQL = sqlMarcada('hap');
  descreve(): string { return `${this.habilitada} ${this.SQL}`; }
}
console.log(new Trava().descreve());
const texto = "olá mundo/a?b=c&d#e ~!*'()";
const enc = encodeURIComponent(texto);
console.log(enc, encodeURI(texto));
console.log(decodeURIComponent(enc) === texto, decodeURI(encodeURI(texto)), decodeURI('%2F%41'));
try { decodeURIComponent('%zz'); } catch (e) { console.log('malformed', (e as Error).message.includes('URI malformed')); }
const brutas: (string | null)[] = ['a', '', 'b', null];
const partes = brutas.filter((p): p is string => Boolean(p));
const n: number | null = 0;
console.log(partes.join('|'), Boolean('x'), Boolean(''), Boolean(n), Boolean(partes.length));
export interface SetorDataSource {
  query<T = Record<string, unknown>>(sql: string, binds?: unknown[]): Promise<T[] | undefined>;
}
interface SetorRow { CD_SETOR: string; N: number }
class Banco {
  chamadas = 0;
  async query<T = any>(sql: string, binds: unknown[] = []): Promise<T> {
    this.chamadas++;
    const raw: object = [{ CD_SETOR: sql.toUpperCase(), N: binds.length }];
    const rows: T = raw as T;
    return rows;
  }
}
async function setor(ds: SetorDataSource, p: string): Promise<string> {
  const rows = (await ds.query<SetorRow>('s-' + p, [p])) ?? [];
  const brutas = (await ds.query('x')) ?? [];
  return rows.map((r) => `${r.CD_SETOR}/${r.N}`).join(',') + ' ' + brutas.length;
}
async function main(): Promise<void> {
  const b = new Banco();
  console.log(await setor(b, 'a'), b.chamadas);
}
main();
