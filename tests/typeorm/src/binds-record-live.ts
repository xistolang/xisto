// XB-182: binds as unknown as unknown[] with binds a Record (a service that names its binds): the object reaches
// TypeORM's query(sql, params) as it is, which binds it by name (Oracle's :name), as on Node; positional binds too.
// binds-record-live.expected: TypeORM 0.3 + node-oracledb 6 (thin) for the same steps
import { DataSource } from "typeorm";

type Binds = Record<string, unknown>;

class Golden {
  constructor(private readonly ds: DataSource) {}
  async consulta(sql: string, binds: Binds): Promise<Array<Record<string, unknown>>> {
    const rows = await this.ds.query<Array<Record<string, unknown>> | undefined>(sql, binds as unknown as unknown[]);
    return Array.isArray(rows) ? rows : [];
  }
}

async function main(): Promise<void> {
  const url = process.env.XISTO_ORACLE_URL ?? "";
  const rest = url.slice("oracle://".length);
  const at = rest.lastIndexOf("@");
  const cred = rest.slice(0, at);
  const where = rest.slice(at + 1);
  const colon = cred.indexOf(":");
  const ds = new DataSource({ type: "oracle", username: cred.slice(0, colon), password: cred.slice(colon + 1), connectString: where });
  await ds.initialize();
  const g = new Golden(ds);
  const corte = 7;
  const binds: Record<string, number> = { a: 1, b: 2 };
  const porNome = await g.consulta("SELECT :a + :b AS S, :corte * 2 AS D FROM DUAL", { ...binds, corte });
  console.log(JSON.stringify(porNome));
  const vazio = await g.consulta("SELECT 1 AS UM FROM DUAL WHERE 1 = :x", { x: 2 });
  console.log(JSON.stringify(vazio));
  const porPosicao = await ds.query("SELECT :1 * 3 AS T FROM DUAL", [14]);
  console.log(JSON.stringify(porPosicao));
  await ds.destroy();
}
main();
