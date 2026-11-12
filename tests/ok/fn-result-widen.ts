// a function value whose result is wider where it goes ((sql, binds) => Promise<unknown> taking this.q, which gives rows):
// an arrow that calls it, as TypeScript lets the function itself go there
async function gravar(query: (sql: string, binds: unknown[]) => Promise<unknown>, sql: string): Promise<string> {
  const r = await query(sql, [1, 2]);
  return JSON.stringify(r);
}
class Servico {
  prefixo = 'db:';
  private readonly q = (sql: string, binds: unknown[]): Promise<Record<string, unknown>[]> =>
  Promise.resolve([{ SQL: this.prefixo + sql, N: binds.length }]);
  async rodar(): Promise<string> {
    return gravar(this.q, 'select 1');
  }
}
async function main(): Promise<void> {
  const q2 = async (sql: string, binds: unknown[]): Promise<string[]> => [sql, String(binds.length)];
  console.log(await new Servico().rodar(), await gravar(q2, 'x'));
}
main();
