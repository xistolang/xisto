// node-oracledb 6 thin, the same operations as outbinds.ts (prints expected-outbinds.txt)
const oracledb = require("oracledb");
const { BIND_IN, BIND_INOUT, BIND_OUT, STRING, NUMBER, DATE, CLOB, BLOB, DB_TYPE_BOOLEAN } = oracledb;
const fs = require("fs");
const src = fs.readFileSync(__dirname + "/outbinds.ts", "utf8");
const SETUP = eval(src.slice(src.indexOf("const SETUP: string[] = [") + 24, src.indexOf("];", src.indexOf("const SETUP")) + 1));
async function main() {
  const m = /^oracle:\/\/([^:]+):([^@]+)@(.+)$/.exec(process.env.XISTO_ORACLE_URL);
  const conn = await oracledb.getConnection({ user: m[1], password: m[2], connectString: m[3] });
  for (const sql of SETUP) await conn.execute(sql);
  const r1 = await conn.execute("BEGIN :ret := xisto_pk.fn_msg(:json, :msg); END;", { json: '{"a":1,"ç":2}', msg: { dir: BIND_OUT, type: STRING, maxSize: 4000 }, ret: { dir: BIND_OUT, type: STRING, maxSize: 1 } });
  console.log("fn_msg", r1.outBinds.ret, r1.outBinds.msg, r1.rowsAffected ?? -1);
  const r2 = await conn.execute("BEGIN xisto_pk.pr_soma(:1, :2, :total, :quando, :txt); END;", { 1: 5, 2: 2.25, total: { dir: BIND_OUT, type: NUMBER }, quando: { dir: BIND_OUT, type: DATE }, txt: { dir: BIND_INOUT, type: STRING, maxSize: 50, val: "olá" } });
  console.log("pr_soma", r2.outBinds.total, r2.outBinds.quando.toISOString(), r2.outBinds.txt);
  const r3 = await conn.execute("begin :1 := 6 * :2; :3 := 'pos ' || :2; end;", [{ dir: BIND_OUT, type: NUMBER }, 7, { dir: BIND_OUT, type: STRING, maxSize: 100 }]);
  console.log("posicional", JSON.stringify(r3.outBinds));
  const r4 = await conn.execute("begin xisto_pk.pr_nulos(:s, :n, :d); end;", { s: { dir: BIND_OUT, type: STRING, maxSize: 10 }, n: { dir: BIND_OUT, type: NUMBER }, d: { dir: BIND_OUT, type: DATE } });
  console.log("nulos", r4.outBinds.s, r4.outBinds.n, r4.outBinds.d);
  for (const n of [4000, 32767]) {
    const r = await conn.execute("begin :1 := xisto_pk.fn_texto(:2); end;", [{ dir: BIND_OUT, type: STRING, maxSize: 32767 }, n]);
    const t = r.outBinds[0];
    console.log("texto", n, t.length, t.slice(0, 12), t.slice(t.length - 5));
  }
  const rc = await conn.execute("begin :1 := xisto_pk.fn_clob(:2); end;", [{ dir: BIND_OUT, type: CLOB }, 5000]);
  const clob = await rc.outBinds[0].getData();
  console.log("clob", clob.length, clob.slice(0, 22), clob.slice(clob.length - 10));
  const rb = await conn.execute("begin :1 := xisto_pk.fn_blob(:2); end;", [{ dir: BIND_OUT, type: BLOB }, 3000]);
  const blob = (await rb.outBinds[0].getData()).toString("hex");
  console.log("blob", blob.length / 2, blob.slice(0, 16), blob.slice(blob.length - 8));
  const re = await conn.execute("begin :1 := xisto_pk.fn_clob(0); end;", [{ dir: BIND_OUT, type: CLOB }]);
  const ev = re.outBinds[0] === null ? null : await re.outBinds[0].getData();
  console.log("clob vazio", JSON.stringify([ev ?? ""]));
  for (const v of [1, -1]) {
    const r = await conn.execute("begin :1 := xisto_pk.fn_bool(:2); end;", [{ dir: BIND_OUT, type: DB_TYPE_BOOLEAN }, v]);
    console.log("bool", v, JSON.stringify(r.outBinds));
  }
  try {
    await conn.execute("begin :1 := xisto_pk.fn_texto(10); end;", [{ dir: BIND_OUT, type: STRING, maxSize: 3 }]);
    console.log("no error");
  } catch (e) { console.log("pequeno", e.message.split("\n")[0]); }
  try { await conn.execute("begin xisto_pk.pr_erro(:1); end;", [42]); } catch (e) { console.log("erro", e.message.split("\n")[0]); }
  const ra = await conn.execute("begin :1 := :2 + 1; end;", [{ dir: BIND_OUT, type: NUMBER }, { dir: BIND_IN, val: 3 }]);
  console.log("depois", JSON.stringify(ra.outBinds));
  let sum = 0;
  for (let i = 0; i < 300; i++) {
    const r = await conn.execute("begin :1 := xisto_pk.fn_clob(:2); end;", [{ dir: BIND_OUT, type: CLOB }, 3]);
    const l = r.outBinds[0];
    sum += (await l.getData()).length;
    l.destroy();
  }
  console.log("300 clobs", sum);
  await conn.execute("drop package xisto_pk");
  await conn.close();
}
main().catch((e) => { console.error(e); process.exit(1); });
