// an any a call gives, where an interface of data goes: a copy through JSON, said with XS2110 at each place
// build: --warnings
// warns: the result of 'db.query' is a copy made through JSON
interface Row { id: number; nome: string }
class Db { async query(sql: string): Promise<any> { return JSON.parse('{"id":1,"nome":"' + sql + '"}'); } }
async function main(): Promise<void> {
  const db = new Db();
  const r: Row = await db.query("a");
  const r2: Row = await db.query("b");
  console.log(r.id, r.nome, r2.nome);
}
main();
