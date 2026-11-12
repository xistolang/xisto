// typeorm: the SQL a repository and a query builder write, for postgres and oracle, through a driver that records
// each statement and its parameters instead of running it (no database needed)
import { Column, DataSource, Driver, DriverSession, Entity, ILike, In, IsNull, LessThanOrEqual, Not, PrimaryColumn, ViewColumn, ViewEntity } from "typeorm";

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

  await repo.update({ id: 7 }, { nome: "Bia", limite: 3.5 });
  await repo.update({ id: 7 }, { nascimento: () => "SYSDATE", nome: "Bia" });
  await repo.delete({ id: 7 });
  await repo.insert({ id: 8, nome: "Caio", limite: 0, ativo: false });
  await repo.insert({ id: 9, nascimento: () => "CURRENT_DATE", nome: "Dani", ativo: true });

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
