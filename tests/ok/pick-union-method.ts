// Pick<A | B, 'query'> (an executor that is a DataSource or an EntityManager): the union, and exec.query<T>(...) runs the
// method of the class the object has (instanceof), each a generic instance for T
class Fonte {
  nome = 'fonte';
  async query<T = unknown>(sql: string, binds: unknown[] = []): Promise<T> {
    const raw: object = [{ SQL: sql, N: binds.length, DE: this.nome }];
    const rows: T = raw as T;
    return rows;
  }
}
class Transacao {
  nome = 'transacao';
  async query<T = unknown>(sql: string, binds: unknown[] = []): Promise<T> {
    const raw: object = [{ SQL: sql.toUpperCase(), N: binds.length, DE: this.nome }];
    const rows: T = raw as T;
    return rows;
  }
}
type Executor = Pick<Fonte | Transacao, 'query'>;
interface Linha { SQL: string; N: number; DE: string }
async function consulta(exec: Executor, cd: number): Promise<string> {
  const rows = await exec.query<Linha[]>(`select ${cd} from dual`, [cd, 'x']);
  return rows.map((r) => `${r.DE}:${r.SQL}:${r.N}`).join(',');
}
async function main(): Promise<void> {
  console.log(await consulta(new Fonte(), 1));
  console.log(await consulta(new Transacao(), 2));
}
main();
