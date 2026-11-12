// A pool's sessionCallback: it runs once per new physical connection, before its first use (not on later checkouts
// of the same one); what it sets (callTimeout) stays with the connection; callback(err) fails that getConnection.
// Same lines as node-oracledb 6 thin.
import { createPool, type Result, type ConnectionAttributes } from "oracledb";

function attrs(): ConnectionAttributes {
  const url = process.env.get("XISTO_ORACLE_URL") ?? "";
  const rest = url.slice(url.indexOf("//") + 2);
  const at = rest.lastIndexOf("@");
  const cred = rest.slice(0, at);
  const colon = cred.indexOf(":");
  return { user: cred.slice(0, colon), password: cred.slice(colon + 1), connectString: rest.slice(at + 1) };
}

async function main(): Promise<void> {
  const a = attrs();
  let calls = 0;
  const pool = await createPool({
    user: a.user, password: a.password, connectString: a.connectString, poolMin: 0, poolMax: 2,
    sessionCallback: (connection: { callTimeout: number }, requestedTag: string, cb: (err?: Error) => void) => {
      calls++;
      connection.callTimeout = 4000 + calls;
      cb();
    },
  });
  for (let i = 0; i < 3; i++) {
    const c = await pool.getConnection();
    console.log(`checkout ${i}: callbacks ${calls}, callTimeout ${c.callTimeout}`);
    c.callTimeout = 0;
    const r: Result<number[]> = await c.execute("select 1 + :1 from dual", [i]);
    console.log(`  query ${r.rows[0][0]}`);
    c.callTimeout = 4001;
    await c.close();
  }
  const c1 = await pool.getConnection();
  const c2 = await pool.getConnection();
  console.log(`two at once: callbacks ${calls}, callTimeouts ${c1.callTimeout} ${c2.callTimeout}`);
  await c1.close();
  await c2.close();
  await pool.close();

  const failing = await createPool({
    user: a.user, password: a.password, connectString: a.connectString, poolMin: 0, poolMax: 1,
    sessionCallback: (connection: { callTimeout: number }, requestedTag: string, cb: (err?: Error) => void) => {
      cb(new Error("session setup refused"));
    },
  });
  try {
    await failing.getConnection();
    console.log("got a connection");
  } catch (e) {
    console.log("getConnection failed: " + (e as Error).message);
  }
  await failing.close();
}
main();
