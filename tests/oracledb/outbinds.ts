// OUT and IN OUT binds of PL/SQL blocks: the same as node-oracledb 6 thin (expected-outbinds.txt).
import { getConnection, BIND_IN, BIND_INOUT, BIND_OUT, STRING, NUMBER, DATE, CLOB, BLOB, DB_TYPE_BOOLEAN, BindParameter, Result } from "oracledb";

const SETUP: string[] = [
  `create or replace package xisto_pk as
     function fn_msg(p_json in varchar2, p_msg out varchar2) return varchar2;
     procedure pr_soma(p_a in number, p_b in number, p_total out number, p_quando out date, p_txt in out varchar2);
     function fn_clob(p_n in number) return clob;
     function fn_blob(p_n in number) return blob;
     procedure pr_nulos(p_s out varchar2, p_n out number, p_d out date);
     function fn_texto(p_n in number) return varchar2;
     function fn_bool(p_n in number) return boolean;
     procedure pr_erro(p_n in number);
   end;`,
  `create or replace package body xisto_pk as
     function fn_msg(p_json in varchar2, p_msg out varchar2) return varchar2 is
     begin
       p_msg := 'recebi ' || length(p_json) || ' caracteres: ' || substr(p_json, 1, 12) || ' ação';
       return case when p_json like '{%' then 'S' else 'N' end;
     end;
     procedure pr_soma(p_a in number, p_b in number, p_total out number, p_quando out date, p_txt in out varchar2) is
     begin
       p_total := p_a + p_b;
       p_quando := to_date('2026-03-08 01:30:00', 'yyyy-mm-dd hh24:mi:ss') + p_a;
       p_txt := upper(p_txt) || '!';
     end;
     function fn_clob(p_n in number) return clob is
       c clob;
     begin
       dbms_lob.createtemporary(c, true);
       for i in 1 .. p_n loop
         dbms_lob.writeappend(c, 10, 'ção-' || lpad(mod(i, 1000), 3, '0') || '.,;');
       end loop;
       return c;
     end;
     function fn_blob(p_n in number) return blob is
       b blob;
     begin
       dbms_lob.createtemporary(b, true);
       for i in 1 .. p_n loop
         dbms_lob.writeappend(b, 4, hextoraw('cafe' || lpad(to_char(mod(i, 256), 'fmXX'), 2, '0') || '00'));
       end loop;
       return b;
     end;
     procedure pr_nulos(p_s out varchar2, p_n out number, p_d out date) is
     begin
       p_s := null; p_n := null; p_d := null;
     end;
     function fn_texto(p_n in number) return varchar2 is
       s varchar2(32767);
     begin
       s := rpad('x', p_n, 'abcdefghij');
       return s;
     end;
     function fn_bool(p_n in number) return boolean is
     begin
       if p_n is null then return null; end if;
       return p_n > 0;
     end;
     procedure pr_erro(p_n in number) is
     begin
       raise_application_error(-20001, 'regra de negócio ' || p_n);
     end;
   end;`,
];

interface Msg {
  ret: string;
  msg: string | null;
}
interface Soma {
  total: number;
  quando: Date;
  txt: string;
}
interface Nulos {
  s: string | null;
  n: number | null;
  d: Date | null;
}

async function main(): Promise<void> {
  const url = process.env.get("XISTO_ORACLE_URL") ?? "";
  const m = /^oracle:\/\/([^:]+):([^@]+)@(.+)$/.exec(url);
  if (m === null) throw new Error("XISTO_ORACLE_URL");
  const conn = await getConnection({ user: m[1], password: m[2], connectString: m[3] });
  for (const sql of SETUP) await conn.execute(sql);

  // a function's return and an OUT string, by name (the bind objects as variables)
  const msgOut: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 4000 };
  const retOut: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 1 };
  const r1: Result<object, Msg> = await conn.executeNamed("BEGIN :ret := xisto_pk.fn_msg(:json, :msg); END;", { json: '{"a":1,"ç":2}', msg: msgOut, ret: retOut });
  const o1 = r1.outBinds;
  if (o1 !== null) console.log("fn_msg", o1.ret, o1.msg, r1.rowsAffected ?? -1);

  // IN by position names ("1", "2") with OUT by name, IN OUT, NUMBER and DATE
  const binds: Record<string, unknown> = {};
  binds["1"] = 5;
  binds["2"] = 2.25;
  const total: BindParameter = { dir: BIND_OUT, type: NUMBER };
  const quando: BindParameter = { dir: BIND_OUT, type: DATE };
  const txt: BindParameter = { dir: BIND_INOUT, type: STRING, maxSize: 50, val: "olá" };
  binds["total"] = total;
  binds["quando"] = quando;
  binds["txt"] = txt;
  const r2: Result<object, Soma> = await conn.executeNamed("BEGIN xisto_pk.pr_soma(:1, :2, :total, :quando, :txt); END;", binds);
  const o2 = r2.outBinds;
  if (o2 !== null) console.log("pr_soma", o2.total, o2.quando.toISOString(), o2.txt);

  // by position: outBinds is an array of the OUT values in order
  const n1: BindParameter = { dir: BIND_OUT, type: NUMBER };
  const s1: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 100 };
  const r3: Result<object, object> = await conn.execute("begin :1 := 6 * :2; :3 := 'pos ' || :2; end;", [n1, 7, s1]);
  console.log("posicional", JSON.stringify(r3.outBinds));

  // NULL OUT values
  const ns: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 10 };
  const nn: BindParameter = { dir: BIND_OUT, type: NUMBER };
  const nd: BindParameter = { dir: BIND_OUT, type: DATE };
  const r4: Result<object, Nulos> = await conn.executeNamed("begin xisto_pk.pr_nulos(:s, :n, :d); end;", { s: ns, n: nn, d: nd });
  const o4 = r4.outBinds;
  if (o4 !== null) console.log("nulos", o4.s, o4.n, o4.d);

  // long strings: 32767 bytes as VARCHAR2, more than that as a CLOB read back
  for (const n of [4000, 32767]) {
    const big: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 32767 };
    const r: Result<object, string[]> = await conn.execute("begin :1 := xisto_pk.fn_texto(:2); end;", [big, n]);
    const t = (r.outBinds ?? [])[0];
    console.log("texto", n, t.length, t.slice(0, 12), t.slice(t.length - 5));
  }

  // CLOB and BLOB OUT (temporary LOBs, read whole)
  const c1: BindParameter = { dir: BIND_OUT, type: CLOB };
  const rc: Result<object, string[]> = await conn.execute("begin :1 := xisto_pk.fn_clob(:2); end;", [c1, 5000]);
  const clob = (rc.outBinds ?? [])[0];
  console.log("clob", clob.length, clob.slice(0, 22), clob.slice(clob.length - 10));
  const b1: BindParameter = { dir: BIND_OUT, type: BLOB };
  const rb: Result<object, string[]> = await conn.execute("begin :1 := xisto_pk.fn_blob(:2); end;", [b1, 3000]);
  const blob = (rb.outBinds ?? [])[0];
  console.log("blob", blob.length / 2, blob.slice(0, 16), blob.slice(blob.length - 8));
  const e1: BindParameter = { dir: BIND_OUT, type: CLOB };
  const re: Result<object, (string | null)[]> = await conn.execute("begin :1 := xisto_pk.fn_clob(0); end;", [e1]);
  console.log("clob vazio", JSON.stringify(re.outBinds));

  // BOOLEAN OUT (23ai)
  const bo: BindParameter = { dir: BIND_OUT, type: DB_TYPE_BOOLEAN };
  for (const v of [1, -1]) {
    const r: Result<object, boolean[]> = await conn.execute("begin :1 := xisto_pk.fn_bool(:2); end;", [bo, v]);
    console.log("bool", v, JSON.stringify(r.outBinds));
  }

  // a value longer than maxSize, and an error from the block
  try {
    const small: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 3 };
    await conn.execute("begin :1 := xisto_pk.fn_texto(10); end;", [small]);
    console.log("no error");
  } catch (e) {
    console.log("pequeno", (e as Error).message.split("\n")[0]);
  }
  try {
    await conn.execute("begin xisto_pk.pr_erro(:1); end;", [42]);
  } catch (e) {
    console.log("erro", (e as Error).message.split("\n")[0]);
  }
  // the connection still works after them, and IN binds of a block still work
  const inb: BindParameter = { dir: BIND_IN, val: 3 };
  const after: BindParameter = { dir: BIND_OUT, type: NUMBER };
  const ra: Result<object, number[]> = await conn.execute("begin :1 := :2 + 1; end;", [after, inb]);
  console.log("depois", JSON.stringify(ra.outBinds));

  // many calls on one connection (temporary LOBs freed as they go)
  let sum = 0;
  for (let i = 0; i < 300; i++) {
    const c: BindParameter = { dir: BIND_OUT, type: CLOB };
    const r: Result<object, string[]> = await conn.execute("begin :1 := xisto_pk.fn_clob(:2); end;", [c, 3]);
    sum = sum + (r.outBinds ?? [])[0].length;
  }
  console.log("300 clobs", sum);

  await conn.execute("drop package xisto_pk");
  await conn.close();
}

main();
