// typeorm relations against a live Oracle (tests/typeorm/run.sh with XISTO_ORACLE_URL): @ManyToOne + @JoinColumn and
// @OneToMany loaded by find's `relations`, the same entities TypeORM 0.3 gives (relations-live.expected is its output
// with node-oracledb 6). Tables of its own, dropped at the end.
import "reflect-metadata";
import { Column, DataSource, Entity, JoinColumn, ManyToOne, OneToMany, PrimaryColumn } from "typeorm";

@Entity("XISTO_TO_REQ")
class Req {
  @PrimaryColumn({ name: "CD_REQ", type: "number" })
  cdReq: number;
  @Column({ name: "CD_EMPRESA", type: "number" })
  cdEmpresa: number;
  @OneToMany(() => Metodo, (m) => m.req)
  metodos: Metodo[];
}

@Entity("XISTO_TO_METODO")
class Metodo {
  @PrimaryColumn({ name: "CD_METODO", type: "number" })
  cdMetodo: number;
  @Column({ name: "CD_REQ", type: "number" })
  cdReq: number;
  @Column({ name: "ID_METODO", type: "varchar2" })
  idMetodo: string;
  @ManyToOne(() => Req, (r) => r.metodos)
  @JoinColumn({ name: "CD_REQ" })
  req: Req;
}

async function main(): Promise<void> {
  const url = process.env.XISTO_ORACLE_URL ?? "";
  const rest = url.slice("oracle://".length);
  const at = rest.lastIndexOf("@");
  const cred = rest.slice(0, at);
  const colon = cred.indexOf(":");
  const ds = new DataSource({ type: "oracle", username: cred.slice(0, colon), password: cred.slice(colon + 1), connectString: rest.slice(at + 1), entities: [Req, Metodo] });
  await ds.initialize();
  for (const t of ["XISTO_TO_METODO", "XISTO_TO_REQ"]) {
    try {
      await ds.query(`DROP TABLE ${t}`, []);
    } catch (e) {
      // (not there yet)
    }
  }
  await ds.query("CREATE TABLE XISTO_TO_REQ (CD_REQ NUMBER(10) PRIMARY KEY, CD_EMPRESA NUMBER(10) NOT NULL)", []);
  await ds.query("CREATE TABLE XISTO_TO_METODO (CD_METODO NUMBER(10) PRIMARY KEY, CD_REQ NUMBER(10) NOT NULL, ID_METODO VARCHAR2(40) NOT NULL)", []);
  await ds.query("INSERT INTO XISTO_TO_REQ VALUES (10, 7)", []);
  await ds.query("INSERT INTO XISTO_TO_REQ VALUES (20, 8)", []);
  await ds.query("INSERT INTO XISTO_TO_REQ VALUES (30, 9)", []);
  await ds.query("INSERT INTO XISTO_TO_METODO VALUES (1, 10, 'a')", []);
  await ds.query("INSERT INTO XISTO_TO_METODO VALUES (2, 10, 'b')", []);
  await ds.query("INSERT INTO XISTO_TO_METODO VALUES (3, 20, 'c')", []);
  const metodos = ds.getRepository<Metodo>(Metodo);
  const one = await metodos.findOne({ where: { cdMetodo: 1, cdReq: 10 }, relations: ["req"] });
  console.log(one === null ? "none" : `${one.idMetodo} -> req ${one.req?.cdReq} empresa ${one.req?.cdEmpresa}`);
  const miss = await metodos.findOne({ where: { cdMetodo: 9 }, relations: ["req"] });
  console.log(miss === null ? "none" : "found");
  const all = await metodos.find({ relations: ["req"], order: { cdMetodo: "ASC" } });
  console.log(all.map((m: Metodo): string => `${m.cdMetodo}:${m.req?.cdEmpresa}`).join(" "));
  const plain = await metodos.find({ order: { cdMetodo: "ASC" } });
  console.log(plain.map((m: Metodo): string => `${m.cdMetodo}:${m.req?.cdEmpresa ?? "no req"}`).join(" "));
  const reqs = await ds.getRepository<Req>(Req).find({ relations: ["metodos"], order: { cdReq: "ASC" } });
  for (const r of reqs) {
    const ids = (r.metodos ?? []).map((m: Metodo): string => m.idMetodo).sort();
    console.log(r.cdReq, r.cdEmpresa, ids.join(",") || "(none)");
  }
  await ds.query("DROP TABLE XISTO_TO_METODO", []);
  await ds.query("DROP TABLE XISTO_TO_REQ", []);
  await ds.destroy();
}
main();
