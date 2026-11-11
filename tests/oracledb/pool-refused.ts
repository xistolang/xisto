// A pool whose connections cannot open (a closed port), asked for more connections at once than poolMax: every
// request fails with NJS-503, none waits forever (a failed open used to leave the queued requests parked)
import { createPool } from "oracledb";

async function main(): Promise<void> {
  const pool = await createPool({ user: "u", password: "p", connectString: "127.0.0.1:1/FREEPDB1", poolMax: 2 });
  const pede = async (n: number): Promise<string> => {
    try {
      const c = await pool.getConnection();
      await c.close();
      return `${n} connected`;
    } catch (err) {
      return `${n} ${(err as Error).message.slice(0, 7)}`;
    }
  };
  for (let round = 0; round < 2; round++) {
    const rs = await Promise.all([pede(1), pede(2), pede(3), pede(4), pede(5), pede(6)]);
    console.log(round, rs.join(" | "));
  }
  await pool.close();
}
main();
