// typeorm against a live Oracle Database through the oracledb package (tests/typeorm/run.sh runs it when
// XISTO_ORACLE_URL is set): type "oracle" with no registerDriver, entities and a view, raw queries with binds,
// the repository, the query builder's OFFSET/FETCH paging, Date columns bound as DATE, transactions on a
// QueryRunner, PL/SQL OUT binds (query, query(.., true), connect()'s connection) and an ORA error with its
// message. Tables of its own, dropped at the end.
import { Column, DataSource, Entity, FindOptionsWhere, ILike, In, LessThanOrEqual, MoreThan, PrimaryColumn, QueryFailedError, ViewColumn, ViewEntity } from "typeorm";
import { BIND_OUT, NUMBER, STRING, BindParameter, Connection, Result } from "oracledb";

interface Structured {
  records: object;
  affected?: int;
}
interface Said {
  msg: string;
}

@Entity("XISTO_TO_ITEM")
class Item {
  @PrimaryColumn({ name: "ID" })
  id: int;
  @Column({ name: "NOME" })
  name: string;
  @Column({ name: "VALOR", type: "number" })
  price: number;
  @Column({ name: "ATIVO" })
  active: boolean;
  @Column({ name: "CRIADO", type: "date" })
  created: Date;
  @Column({ name: "NOTA", nullable: true })
  note: string | null;
}

@ViewEntity("XISTO_TO_ATIVOS")
class Active {
  @ViewColumn({ name: "ID" })
  id: int;
  @ViewColumn({ name: "NOME" })
  name: string;
}

interface Total {
  TOTAL: number;
}

function day(d: Date): string {
  return d.toISOString().slice(0, 10);
}

async function main(): Promise<void> {
  const url = process.env.get("XISTO_ORACLE_URL") ?? "";
  // oracle://user:password@host:port/service
  const rest = url.slice("oracle://".length);
  const at = rest.lastIndexOf("@");
  const cred = rest.slice(0, at);
  const where = rest.slice(at + 1);
  const colon = cred.indexOf(":");
  let sessions = 0;
  const ds = new DataSource({
    type: "oracle", username: cred.slice(0, colon), password: cred.slice(colon + 1), connectString: where,
    // node-oracledb's sessionCallback, passed on by TypeORM: once per new physical connection
    extra: {
      sessionCallback: (connection: { callTimeout: number }, _requestedTag: string, cb: (err?: Error) => void) => {
        sessions++;
        connection.callTimeout = 0;
        cb();
      },
    },
  });
  await ds.initialize();
  try {
    await ds.query("DROP VIEW XISTO_TO_ATIVOS", []);
  } catch (e) {
    // (not there yet)
  }
  try {
    await ds.query("DROP TABLE XISTO_TO_ITEM", []);
  } catch (e) {
    // (not there yet)
  }
  await ds.query("CREATE TABLE XISTO_TO_ITEM (ID NUMBER(10) PRIMARY KEY, NOME VARCHAR2(40) NOT NULL, VALOR NUMBER(10,2) NOT NULL, ATIVO NUMBER(1) NOT NULL, CRIADO DATE NOT NULL, NOTA VARCHAR2(40))", []);
  await ds.query("CREATE VIEW XISTO_TO_ATIVOS AS SELECT ID, NOME FROM XISTO_TO_ITEM WHERE ATIVO = 1", []);
  const repo = ds.getRepository<Item>(Item);

  const a: Item = { id: 1, name: "apple", price: 2.5, active: true, created: new Date("2026-01-10T00:00:00Z"), note: null };
  const b: Item = { id: 2, name: "bread", price: 7.25, active: true, created: new Date("2026-02-20T00:00:00Z"), note: "fresh" };
  const c: Item = { id: 3, name: "cheese", price: 30, active: false, created: new Date("2026-03-30T00:00:00Z"), note: null };
  await repo.save(a);
  await repo.save(b);
  await repo.save(c);
  b.price = 8;
  await repo.save(b); // the row exists: UPDATE
  console.log("count " + String(await repo.count()));
  // save(array): every entity in one transaction, the saved entities back in order
  const more = await repo.save([
    { id: 4, name: "dates", price: 12, active: true, created: new Date("2026-04-01T00:00:00Z"), note: null },
    { id: 5, name: "eggs", price: 3, active: false, created: new Date("2026-05-01T00:00:00Z"), note: "dozen" },
  ]);
  console.log("saved " + more.map((i: Item): string => i.name + "#" + String(i.id)).join(",") + ", count " + String(await repo.count()));
  await repo.delete({ id: In([4, 5]) });
  // where from a variable that may be undefined (no filter)
  for (const term of ["EA", ""]) {
    const where: FindOptionsWhere<Item> | undefined = term ? { name: ILike(`%${term}%`) } : undefined;
    const got = await repo.find({ where, order: { id: "ASC" } });
    console.log("where '" + term + "' " + got.map((i: Item): string => i.name).join(","));
  }

  const cheap = await repo.find({ where: { active: true, price: LessThanOrEqual(10) }, order: { name: "DESC" } });
  for (const i of cheap) {
    console.log(i.name + " " + String(i.price) + " " + String(i.active) + " " + day(i.created) + " " + (i.note ?? "-"));
  }
  const one = await repo.findOneBy({ id: 3 });
  console.log(one === null ? "none" : one.name + " active=" + String(one.active));
  const none = await repo.findOneBy({ id: 99 });
  console.log(none === null ? "no 99" : "found 99");
  const some = await repo.find({ where: { id: In([1, 2, 3]) }, order: { id: "ASC" }, take: 1, skip: 1 });
  console.log("page " + some[0].name);

  // a Date column in where binds as DATE: rows created after February 1st
  const recent = await repo.find({ where: { created: MoreThan(new Date("2026-02-01T00:00:00Z")) }, order: { id: "ASC" } });
  console.log("after feb " + recent.map((i: Item): string => i.name).join(","));

  const upd = await repo.update({ id: 1 }, { note: "red" });
  console.log("updated " + String(upd.affected));
  const qb = await repo.createQueryBuilder("i").where("i.price > :min", { min: 5 }).orderBy("i.price", "ASC").skip(1).take(1).getMany();
  console.log("qb page " + qb.map((i: Item): string => i.name).join(","));
  console.log("qb count " + String(await repo.createQueryBuilder("i").where("i.active = :on", { on: 1 }).getCount()));
  const raw: Total[] = await ds.query("SELECT SUM(VALOR) AS TOTAL FROM XISTO_TO_ITEM WHERE ID IN (:1, :2)", [1, 2]);
  console.log("sum " + String(raw[0].TOTAL));

  const ativos = await ds.getRepository<Active>(Active).find({ order: { id: "ASC" } });
  console.log("view " + ativos.map((v: Active): string => String(v.id) + ":" + v.name).join(","));

  const qr = ds.createQueryRunner();
  await qr.connect();
  await qr.startTransaction();
  await qr.query("UPDATE XISTO_TO_ITEM SET NOME = 'kept' WHERE ID = :1", [1]);
  await qr.commitTransaction();
  await qr.startTransaction();
  await qr.query("UPDATE XISTO_TO_ITEM SET NOME = 'lost' WHERE ID = :1", [2]);
  await qr.rollbackTransaction();
  await qr.release();
  const after = await repo.find({ order: { id: "ASC" } });
  console.log("after tx " + after.map((i: Item): string => i.name).join(","));

  try {
    await ds.transaction(async (m) => {
      await m.query("UPDATE XISTO_TO_ITEM SET NOME = 'never' WHERE ID = :1", [3]);
      throw new Error("abort");
    });
  } catch (e) {
    console.log("transaction rolled back: " + e.message);
  }
  const three = await repo.findOneBy({ id: 3 });
  console.log("3 is " + (three === null ? "gone" : three.name));
  const counted = await ds.transaction(async (m) => {
    const rows: { N: number }[] = await m.query("SELECT COUNT(*) AS N FROM XISTO_TO_ITEM WHERE ID = :1", [3]);
    return rows[0].N + 6;
  });
  console.log("transaction returned " + String(counted));
  const viaManager = await ds.transaction(async (m) => {
    await m.update(Item, { id: 3 }, { name: "brie" });
    const it = await m.findOne(Item, { where: { id: 3 } });
    const n = await m.count(Item, { where: { id: 3 } });
    const back = await m.findOneBy(Item, { id: 3 });
    await m.update(Item, { id: 3 }, { name: back === null ? "?" : "cheese" });
    return (it === null ? "none" : it.name) + " " + String(n);
  });
  console.log("manager " + viaManager);

  const del = await repo.delete({ id: In([1, 2]) });
  console.log("deleted " + String(del.affected) + ", left " + String(await repo.count()));
  try {
    await ds.query("SELECT * FROM XISTO_TO_NO_SUCH_TABLE", []);
  } catch (e) {
    console.log("error: " + e.message.split("\n")[0]);
    if (e instanceof QueryFailedError) console.log("error fields " + e.name + " " + e.code + " " + String(e.errorNum) + " " + String(e.offset));
  }
  // PL/SQL OUT binds: query() returns outBinds, query(sql, binds, true) a QueryResult, connect() the oracledb connection
  const ret: BindParameter = { dir: BIND_OUT, type: NUMBER };
  const viaDs: number[] = await ds.query("BEGIN :1 := :2 * 3; END;", [ret, 14]);
  console.log("outBinds " + JSON.stringify(viaDs));
  const qr2 = ds.createQueryRunner();
  const upd2: Structured = await qr2.query("UPDATE XISTO_TO_ITEM SET NOME = NOME WHERE ID = :1", [3], true);
  console.log("structured update affected " + String(upd2.affected ?? -1) + " records " + JSON.stringify(upd2.records));
  const pl: Structured = await qr2.query("BEGIN :1 := 'pl ' || :2; END;", [{ dir: BIND_OUT, type: STRING, maxSize: 20 } as BindParameter, 5], true);
  console.log("structured plsql records " + JSON.stringify(pl.records));
  const conn = (await qr2.connect()) as Connection;
  const msg: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 100 };
  const said: Result<object, Said> = await conn.executeNamed("BEGIN :msg := 'olá ' || :who; END;", { who: "typeorm", msg });
  console.log("raw connection " + (said.outBinds === null ? "none" : said.outBinds.msg));
  await qr2.release();
  await ds.query("DROP VIEW XISTO_TO_ATIVOS", []);
  await ds.query("DROP TABLE XISTO_TO_ITEM", []);
  console.log("session callbacks ran: " + String(sessions > 0));
  await ds.destroy();
}
main();
