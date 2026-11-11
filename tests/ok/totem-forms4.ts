// Object.prototype.hasOwnProperty.call(o, k) is k in o; x.m(args) on a union of classes with any arguments (copied per branch)
const COLS: Record<string, string> = { nome: 'NM', cd: 'CD' };
function coluna(k: string): string { return Object.prototype.hasOwnProperty.call(COLS, k) ? (COLS[k] ?? '') : '?'; }
console.log(coluna('nome'), coluna('x'), coluna('toString'));
class Fonte2 {
  nome = 'fonte';
  async query<T = unknown>(sql: string, binds: unknown[] = []): Promise<T> {
    const raw: object = [{ SQL: sql, N: binds.length, DE: this.nome }];
    const rows: T = raw as T;
    return rows;
  }
}
class Transacao2 {
  nome = 'transacao';
  async query<T = unknown>(sql: string, binds: unknown[] = []): Promise<T> {
    const raw: object = [{ SQL: sql.toUpperCase(), N: binds.length, DE: this.nome }];
    const rows: T = raw as T;
    return rows;
  }
}
type Executor = Pick<Fonte2 | Transacao2, 'query'>;
interface Linha { SQL: string; N: number; DE: string }
async function consulta(exec: Executor, cd: number): Promise<string> {
  const cols = ["a", "b"];
  const rows = await exec.query<Linha[]>(`select ${cols.join(", ")} ${cd} from dual`, [cd, "x"] as unknown as unknown[]);
  return rows.map((r) => `${r.DE}:${r.SQL}:${r.N}`).join(',');
}
async function principal(): Promise<void> {
  console.log(await consulta(new Fonte2(), 1));
  console.log(await consulta(new Transacao2(), 2));
}

principal();
