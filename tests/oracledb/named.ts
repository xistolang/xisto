// execute() with binds by name as an object (node-oracledb's form), next to binds by position; expected-named.txt is
// what node-oracledb 6 thin prints for the same program against the same server
import { getConnection, Connection, ConnectionAttributes, BIND_OUT, NUMBER, STRING, OUT_FORMAT_OBJECT } from "oracledb";

function attrs(): ConnectionAttributes {
  const url = process.env.get("XISTO_ORACLE_URL") ?? "";
  const rest = url.slice(url.indexOf("//") + 2);
  const at = rest.lastIndexOf("@");
  const cred = rest.slice(0, at);
  const colon = cred.indexOf(":");
  return { user: cred.slice(0, colon), password: cred.slice(colon + 1), connectString: rest.slice(at + 1) };
}

async function main(): Promise<void> {
  const conn: Connection = await getConnection(attrs());
  const r = await conn.execute("select :a + :b as s, :a as a from dual", { a: 2, b: 40 }, { outFormat: OUT_FORMAT_OBJECT });
  console.log(JSON.stringify((r as { rows: unknown[] }).rows));
  const p = await conn.execute(
    `BEGIN :total := :x * 2; :txt := 'n=' || :x; END;`,
    {
      x: 21,
      total: { dir: BIND_OUT, type: NUMBER },
      txt: { dir: BIND_OUT, type: STRING, maxSize: 100 },
    },
  );
  console.log(JSON.stringify((p as { outBinds: unknown }).outBinds));
  const q = await conn.execute("select :1 as one from dual", [7], { outFormat: OUT_FORMAT_OBJECT });
  console.log(JSON.stringify((q as { rows: unknown[] }).rows));
  // the result with no declared row type: its rows read by position (node-oracledb's Result<any>)
  const u = await conn.execute("SELECT USER FROM DUAL");
  const who = u?.rows?.[0]?.[0] as string;
  console.log(typeof who, who, u.rows?.length);
  await conn.close();
}
main();
