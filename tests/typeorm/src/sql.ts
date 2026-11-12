// typeorm: the SQL a repository and a query builder write, for postgres and oracle, through a driver that records
// each statement and its parameters instead of running it (no database needed)
import { Column, DataSource, Driver, DriverSession, Entity, FindOptionsWhere, ILike, In, IsNull, LessThanOrEqual, Not, PrimaryColumn, Raw, ViewColumn, ViewEntity } from "typeorm";

@Entity("TB_BENEFICIARIO")
class Beneficiario {
  @PrimaryColumn({ name: "CD_BENEFICIARIO", type: "number" })
  id: int;
  @Column({ name: "NM_BENEFICIARIO", type: "varchar2", length: 100 })
  nome: string;
  @Column({ name: "DT_NASCIMENTO", type: "date", nullable: true })
  nascimento: string | null;
  @Column({ name: "VL_LIMITE", type: "number", precision: 12, scale: 2, default: 0 })
  limite: number;
  @Column({ name: "FL_ATIVO", type: "number" })
  ativo: boolean;
}

@ViewEntity({ name: "VW_RESUMO", synchronize: false })
class Resumo {
  @ViewColumn({ name: "CD_GRUPO" })
  grupo: int;
  @ViewColumn({ name: "QT_TOTAL" })
  total: int;
}

class RecordingDriver extends Driver {
  name: string;
  rows: string;
  constructor(name: string, rows: string) {
    super();
    this.name = name;
    this.rows = rows;
  }
  dialect(): string {
    return this.name;
  }
  async connect(): Promise<void> {}
  async close(): Promise<void> {}
  async run(sql: string, params: unknown[]): Promise<object> {
    console.log("  " + sql);
    console.log("  params " + String(params.length));
    const rows = sql.startsWith("SELECT COUNT") ? '[{"cnt":3}]' : this.rows;
    const r: object = JSON.parse('{"rows":' + rows + ',"rowCount":1}');
    return r;
  }
  async session(): Promise<DriverSession> {
    throw new Error("no sessions in this test");
  }
}

async function dialect(name: string): Promise<void> {
  console.log("== " + name);
  const rows = '[{"CD_BENEFICIARIO":7,"NM_BENEFICIARIO":"Ana","DT_NASCIMENTO":null,"VL_LIMITE":"12.5","FL_ATIVO":1}]';
  const ds = DataSource.withDriver({ type: name }, new RecordingDriver(name, rows));
  await ds.initialize();
  const repo = ds.getRepository<Beneficiario>(Beneficiario);

  const found = await repo.find({ where: { ativo: true, limite: LessThanOrEqual(100) }, order: { nome: "DESC" }, skip: 20, take: 10 });
  console.log(found[0].nome + " " + String(found[0].id) + " " + String(found[0].limite) + " " + String(found[0].ativo) + " " + (found[0].nascimento ?? "sem data"));

  const one = await repo.findOne({ where: [{ nome: ILike("an%") }, { id: In([1, 2, 3]) }] });
  console.log(one === null ? "none" : one.nome);

  await repo.findOneBy({ nascimento: IsNull() });
  await repo.find({ where: { nome: Not("x") } });
  console.log("count " + String(await repo.count({ where: { ativo: true } })));

  // Raw: the SQL TypeORM 0.3 writes (Oracle, checked with getQueryAndParameters on Node; parentheses aside)
  await repo.find({ where: { nome: Raw((alias) => `UPPER(${alias}) LIKE UPPER(:term) ESCAPE '\\'`, { term: "%an%" }) } });
  await repo.find({ where: [{ id: Raw((alias) => `TO_CHAR(${alias}) LIKE :code AND ${alias} IN (SELECT X FROM T WHERE O = :op)`, { code: "%4%", op: "ADM" }) }, { ativo: true }] });
  await repo.update({ nascimento: Raw((alias) => `${alias} >= TRUNC(SYSDATE) AND ${alias} < TRUNC(SYSDATE) + 1`), id: In([0, 1]) }, { ativo: true });
  await repo.update({ id: 7 }, { nome: "Bia", limite: 3.5 });
  // the manager with the entity given by its table's name (TypeORM's EntityTarget as a string) and by its class
  await ds.manager.update("TB_BENEFICIARIO" as never, { id: 7 }, { nome: "Cris" });
  await ds.manager.update(Beneficiario, { id: 8 }, { nome: "Duda" });
  const byName = await ds.manager.findOne("TB_BENEFICIARIO" as never, { where: { id: 7 } } as never);
  console.log(byName ? "found by name" : "none");
  console.log("count by name " + String(await ds.manager.count("TB_BENEFICIARIO" as never, { where: { ativo: true } } as never)));
  await ds.manager.insert("TB_BENEFICIARIO" as never, { id: 10, nome: "Eva" } as never);
  await repo.update({ id: 7 }, { nascimento: () => "SYSDATE", nome: "Bia" });
  await repo.delete({ id: 7 });
  await repo.insert({ id: 8, nome: "Caio", limite: 0, ativo: false });
  await repo.insert({ id: 9, nascimento: () => "CURRENT_DATE", nome: "Dani", ativo: true });

  // where conditions built by property name, then given to a query builder (setFindOptions), as a service does
  // (the same SQL as TypeORM 0.3 getQueryAndParameters, up to parentheses; TypeORM writes the boolean as 1)
  const where: FindOptionsWhere<Beneficiario>[] = [{ nome: Raw((alias) => `UPPER(${alias}) LIKE :t`, { t: "%A%" }) }, {}];
  where[0].ativo = true;
  where[1].id = In([4, 5]);
  const fq = repo.createQueryBuilder("b").setFindOptions({ where });
  const fa = fq.escape(fq.alias);
  const [fl, ft] = await fq.orderBy(`${fa}.NM_BENEFICIARIO`, "DESC").offset(10).limit(5).getManyAndCount();
  console.log("setFindOptions: " + String(fl.length) + " " + String(ft));
  const qb = repo.createQueryBuilder("b")
    .select(["b.nome", "b.id"])
    .innerJoin(Resumo, "r", "r.grupo = b.id")
    .where("b.ativo = :ativo", { ativo: true })
    .andWhere("b.id IN (:...ids)", { ids: [1, 2] })
    .andWhere("b.nome LIKE 'x:y'")
    .orderBy("b.nome", "DESC")
    .skip(5)
    .take(5);
  console.log("  qb: " + qb.getSql());

  const upd = ds.createQueryBuilder<Beneficiario>(Beneficiario, "b").update(Beneficiario).set({ nome: "Z", ativo: false }).where("CD_BENEFICIARIO = :id", { id: 9 });
  console.log("  update: " + upd.getSql());
  const ins = ds.createQueryBuilder<Beneficiario>(Beneficiario, "b").insert().into(Beneficiario).values([{ id: 1, nome: "A" }, { id: 2, nome: "B" }]);
  console.log("  insert: " + ins.getSql());
  // a function's result is raw SQL, in its place among the columns (TypeORM 0.3: SET "NM_BENEFICIARIO" = USER, ...;
  // VALUES (:orm_param_0, USER, :orm_param_1))
  const updRaw = ds.createQueryBuilder<Beneficiario>(Beneficiario, "b").update(Beneficiario).set({ nome: () => "USER", ativo: false }).where("CD_BENEFICIARIO = :id", { id: 9 });
  console.log("  update raw: " + updRaw.getSql());
  const insRaw = ds.createQueryBuilder<Beneficiario>(Beneficiario, "b").insert().into(Beneficiario).values({ id: 1, nome: () => "USER", ativo: true });
  console.log("  insert raw: " + insRaw.getSql());
  // where((sub) => ... sub.subQuery()...getQuery()): TypeORM 0.3 writes WHERE "b"."CD_BENEFICIARIO" IN (SELECT
  // o.CD_BENEFICIARIO FROM "TB_OUTRA" "o" WHERE o.CD_LOCAL = :1 AND o.FL_TIPO = 0) [7] (here with parentheses around each condition)
  const sq = ds.createQueryBuilder<Beneficiario>(Beneficiario, "b").orderBy("b.nome", "ASC").skip(0).take(5);
  sq.where((sub) => "b.id IN " + sub.subQuery().select("o.CD_BENEFICIARIO").from("TB_OUTRA", "o").where("o.CD_LOCAL = :cdLocal AND o.FL_TIPO = 0").getQuery(), { cdLocal: 7 });
  console.log("  subquery: " + sq.getSql());
  console.log("  updateEntity: " + ds.createQueryBuilder<Beneficiario>(Beneficiario, "b").insert().into(Beneficiario).values({ id: 2, nome: "N", ativo: true }).updateEntity(false).getSql());
  const del = ds.createQueryBuilder<Beneficiario>(Beneficiario, "b").delete().from(Beneficiario, "b").where("b.id = :id", { id: 3 });
  console.log("  delete: " + del.getSql());

  const raw: Resumo[] = await ds.query(name === "oracle" ? "SELECT CD_GRUPO AS \"grupo\", QT_TOTAL AS \"total\" FROM VW_RESUMO WHERE CD_GRUPO = :1" : 'SELECT 1 WHERE $1 = 1', [1]);
  console.log("raw rows " + String(raw.length));
}

async function main(): Promise<void> {
  await dialect("postgres");
  await dialect("oracle");
}
main();
