// packages/oracledb against a live Oracle Database (XISTO_ORACLE_URL=oracle://user:password@host:port/service).
// node-oracledb 6 thin prints the same lines for the same operations (checked by hand against Oracle 23ai Free).
import { createPool, getConnection, Connection, ConnectionAttributes, Pool, OUT_FORMAT_OBJECT, OUT_FORMAT_ARRAY, Result } from "oracledb";

function attrs(): ConnectionAttributes {
  const url = process.env.get("XISTO_ORACLE_URL") ?? "";
  const rest = url.slice(url.indexOf("//") + 2);
  const at = rest.lastIndexOf("@");
  const cred = rest.slice(0, at);
  const colon = cred.indexOf(":");
  return { user: cred.slice(0, colon), password: cred.slice(colon + 1), connectString: rest.slice(at + 1) };
}

interface Typed { N1: number; N2: number; N3: number; N4: number; N5: number; S: string; C: string; D: Date; TS: Date; TZ: Date; R: string; NUL: string | null; U: string }
interface Item { ID: number; NAME: string | null; QTY: number | null; CREATED: Date | null; NOTES: string | null; PIC: string | null }
interface Count { N: number }
interface Named { ID: number; NAME: string | null }
interface Qty { QTY: number | null }
interface When { CREATED: Date | null }

async function tryRun(conn: Connection, sql: string): Promise<void> {
  try {
    await conn.execute(sql);
  } catch (e) {
    console.log("error:", (e as Error).message.split("\n")[0]);
  }
}

function iso(d: Date | null): string {
  if (d === null) return "null";
  return d.toISOString();
}

async function countMod(pool: Pool, k: int): Promise<number> {
  const pc = await pool.getConnection();
  const pr: Result<Count> = await pc.execute("select count(*) as n from xisto_items where mod(id, 6) = :1", [k], { outFormat: OUT_FORMAT_OBJECT });
  await pc.close();
  return pr.rows[0].N;
}

async function main(): Promise<void> {
  const a = attrs();
  const conn = await getConnection(a);
  try {
    await conn.execute("drop table xisto_items purge");
  } catch (e) {
    // (absent on the first run)
  }
  await conn.execute("create table xisto_items (id number(10) constraint xisto_items_pk primary key, name varchar2(100), qty number(12,3), created date, notes clob, pic blob)");

  // types
  const t: Result<Typed> = await conn.execute(
    "select 42 as n1, -0.5 as n2, 0.001 as n3, 123456789012345678901234567890 as n4, -987654.25 as n5, 'texto' as s, cast('ab' as char(4)) as c, " +
    "date '2026-11-09' as d, timestamp '2026-11-09 13:45:10.123' as ts, timestamp '2026-11-09 13:45:10 +02:00' as tz, hextoraw('00ff10') as r, null as nul, 'ação 日本 €' as u from dual",
    [], { outFormat: OUT_FORMAT_OBJECT });
  console.log("types:", JSON.stringify(t.rows));

  // DML with positional binds, then named, rowsAffected, autoCommit off
  const big = "x".repeat(40000);
  const r1: Result<Count> = await conn.execute("insert into xisto_items (id, name, qty, created, notes) values (:1, :2, :3, :4, :5)", [1, "um", 1.5, new Date(Date.UTC(2026, 0, 2, 3, 4, 5)), "nota curta"]);
  console.log("insert:", r1.rowsAffected);
  await conn.execute("insert into xisto_items (id, name, qty, notes, pic) values (:1, :2, :3, :4, hextoraw('cafe'))", [2, "dois", null, big]);
  const r2: Result<Count> = await conn.executeNamed("update xisto_items set qty = :q where id = :id or name = :name", { q: 7, id: 2, name: "nenhum" });
  console.log("update:", r2.rowsAffected);
  const items: Result<Item> = await conn.execute("select id, name, qty, created, notes, pic from xisto_items order by id", [], { outFormat: OUT_FORMAT_OBJECT });
  for (const it of items.rows) console.log("item:", it.ID, it.NAME, it.QTY, iso(it.CREATED), it.NOTES === null ? "null" : String(it.NOTES.length), it.PIC);

  // transaction: rollback undoes, commit keeps
  await conn.rollback();
  const c0: Result<Count> = await conn.execute("select count(*) as n from xisto_items", [], { outFormat: OUT_FORMAT_OBJECT });
  console.log("after rollback:", c0.rows[0].N);
  await conn.execute("insert into xisto_items (id, name) select level + 10, 'linha ' || mod(level, 3) from dual connect by level <= 250");
  await conn.commit();

  // many rows (fetched 100 at a time), repeated values, arrays, maxRows
  const many: Result<number[]> = await conn.execute("select id, mod(id, 2) from xisto_items order by id", [], { outFormat: OUT_FORMAT_ARRAY });
  let sum = 0;
  for (const row of many.rows) sum += row[0] + row[1];
  console.log("many:", many.rows.length, sum, JSON.stringify(many.rows[0]), JSON.stringify(many.rows[249]));
  const few: Result<Named> = await conn.execute("select id, name from xisto_items order by id", [], { outFormat: OUT_FORMAT_OBJECT, maxRows: 3 });
  console.log("maxRows:", JSON.stringify(few.rows));

  // errors keep the connection usable
  await tryRun(conn, "select * from no_such_table");
  await tryRun(conn, "insert into xisto_items (id) values (11)");
  await tryRun(conn, "select 1/0 from dual");

  // PL/SQL with binds, DDL
  await conn.execute("begin update xisto_items set qty = :1 where id = :2; end;", [99, 11]);
  const pl: Result<Qty> = await conn.execute("select qty from xisto_items where id = 11", [], { outFormat: OUT_FORMAT_OBJECT });
  console.log("plsql:", JSON.stringify(pl.rows));

  // cursors are closed: many statements on one connection
  let n = 0;
  for (let i = 0; i < 1500; i++) {
    const q: Result<Count> = await conn.execute("select :1 as n from dual", [i], { outFormat: OUT_FORMAT_OBJECT });
    n += q.rows[0].N;
  }
  console.log("cursors:", n);

  // dates bound and read back
  await conn.execute("update xisto_items set created = :1 where id = 12", [new Date(Date.UTC(2026, 5, 30, 23, 59, 58, 250))]);
  const dt: Result<When> = await conn.execute("select created from xisto_items where id = 12", [], { outFormat: OUT_FORMAT_OBJECT });
  console.log("date:", iso(dt.rows[0].CREATED));
  await conn.commit();
  await conn.close();

  // a pool: connections in parallel
  const pool = await createPool({ user: a.user, password: a.password, connectString: a.connectString, poolMax: 3 });
  const counts: number[] = await Promise.all([countMod(pool, 0), countMod(pool, 1), countMod(pool, 2), countMod(pool, 3), countMod(pool, 4), countMod(pool, 5)]);
  counts.sort((x, y) => x - y);
  console.log("pool:", JSON.stringify(counts));
  const last = await pool.getConnection();
  await last.execute("drop table xisto_items purge");
  await last.close();
  await pool.close();

  // a wrong password
  try {
    const bad = await getConnection({ user: a.user, password: (a.password ?? "") + "x", connectString: a.connectString });
    await bad.close();
  } catch (e) {
    console.log("login:", (e as Error).message.split("\n")[0]);
  }
  console.log("done");
}
main();
