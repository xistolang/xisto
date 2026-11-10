// pg: more queries at once than the pool has connections (Promise.all over 12 with max 2). The ones that wait are
// handed a connection as others finish; each answer must come back to its own query, in order.
import { Pool, QueryResult } from "pg";

interface Row {
  n: string;
}

async function one(pool: Pool, i: int): Promise<string> {
  const r: QueryResult<Row> = await pool.query("SELECT pg_sleep(0.02), ($1::int * 10)::text AS n", [i]);
  return r.rows[0].n;
}

async function main(): Promise<void> {
  const port = Number(process.env.get("PGPORT_TEST") ?? "55432");
  const pool = new Pool({ host: "127.0.0.1", port: Math.trunc(port), user: "xisto", password: "xisto", database: "conduit", max: 2 });
  const tasks: Promise<string>[] = [];
  for (let i = 0; i < 12; i++) {
    tasks.push(one(pool, i));
  }
  const all = await Promise.all(tasks);
  console.log(all.join(" "));
  const again = await Promise.all([one(pool, 100), one(pool, 101), one(pool, 102)]);
  console.log(again.join(" "));
}
main();
