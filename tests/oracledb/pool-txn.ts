// A pooled connection goes back without an open transaction: release rolls back only when the server's call status
// says one is open (a round trip saved on every read-only checkout), and an uncommitted change never reaches the next
// borrower of the same physical connection (poolMax 1). Same lines as node-oracledb 6 thin.
import { createPool, Pool, Result, OUT_FORMAT_OBJECT, ConnectionAttributes } from "oracledb";

interface Row { N: number; V: string | null }

function attrs(): ConnectionAttributes {
  const url = process.env.get("XISTO_ORACLE_URL") ?? "";
  const rest = url.slice(url.indexOf("//") + 2);
  const at = rest.lastIndexOf("@");
  const cred = rest.slice(0, at);
  const colon = cred.indexOf(":");
  return { user: cred.slice(0, colon), password: cred.slice(colon + 1), connectString: rest.slice(at + 1) };
}

async function seen(pool: Pool, label: string): Promise<void> {
  const c = await pool.getConnection();
  const r: Result<Row> = await c.execute("select count(*) as n, max(v) as v from xisto_txn", [], { outFormat: OUT_FORMAT_OBJECT });
  console.log(`${label}: ${r.rows[0].N} rows, v=${r.rows[0].V}`);
  await c.close();
}

async function main(): Promise<void> {
  const a = attrs();
  const pool = await createPool({ user: a.user, password: a.password, connectString: a.connectString, poolMin: 0, poolMax: 1 });
  const c0 = await pool.getConnection();
  try {
    await c0.execute("drop table xisto_txn purge");
  } catch (e) {
    // first run: no table yet
  }
  await c0.execute("create table xisto_txn (id number primary key, v varchar2(10))");
  await c0.close();
  await seen(pool, "empty");

  const c1 = await pool.getConnection();
  await c1.execute("insert into xisto_txn values (1, 'a')");
  await c1.close();
  await seen(pool, "insert, no commit, released");

  const c2 = await pool.getConnection();
  await c2.execute("insert into xisto_txn values (1, 'a')");
  await c2.commit();
  await c2.close();
  await seen(pool, "insert, commit, released");

  const c3 = await pool.getConnection();
  await c3.execute("update xisto_txn set v = 'b' where id = 1");
  await c3.close();
  await seen(pool, "update, no commit, released");

  const c4 = await pool.getConnection();
  await c4.execute("update xisto_txn set v = 'c' where id = 1", [], { autoCommit: true });
  await c4.close();
  await seen(pool, "update, autoCommit, released");

  const c5 = await pool.getConnection();
  await c5.execute("update xisto_txn set v = 'd' where id = 1");
  try {
    await c5.execute("insert into xisto_txn values (1, 'x')");
  } catch (e) {
    console.log("error:", (e as Error).message.slice(0, 9));
  }
  await c5.close();
  await seen(pool, "update, failed insert, released");

  const c6 = await pool.getConnection();
  await c6.execute("drop table xisto_txn purge");
  await c6.close();
  await pool.close();
}
main();
