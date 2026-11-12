// typeorm's OracleDriver over node-oracledb's shape, given as functions: results normalized from rows/rowsAffected,
// a statement outside a transaction committed with it (autocommit) and its connection closed, a QueryRunner's commit/rollback/release
// on one pinned connection (no database: the functions record what they are asked)
import { Column, DataSource, Entity, OracleConnectionFunctions, OracleDriver, OraclePoolFunctions, PrimaryColumn } from "typeorm";

@Entity("TB_PLANO")
class Plano {
  @PrimaryColumn({ name: "CD_PLANO" })
  id: int;
  @Column({ name: "DS_PLANO" })
  descricao: string;
}

let opened: int = 0;
const log: string[] = [];

function connection(): OracleConnectionFunctions {
  const n = opened;
  opened = opened + 1;
  return {
    execute: async (sql: string, binds: unknown[], autoCommit: boolean): Promise<object> => {
      log.push("c" + String(n) + " execute " + sql + " [" + String(binds.length) + "]" + (autoCommit ? " autocommit" : ""));
      if (sql.startsWith("SELECT COUNT")) {
        const r: object = JSON.parse('{"rows":[{"cnt":4}]}');
        return r;
      }
      if (sql.startsWith("SELECT")) {
        const r: object = JSON.parse('{"rows":[{"CD_PLANO":10,"DS_PLANO":"Ouro"},{"CD_PLANO":11,"DS_PLANO":"Prata"}]}');
        return r;
      }
      const r: object = JSON.parse('{"rowsAffected":2}');
      return r;
    },
    commit: async (): Promise<void> => {
      log.push("c" + String(n) + " commit");
    },
    rollback: async (): Promise<void> => {
      log.push("c" + String(n) + " rollback");
    },
    close: async (): Promise<void> => {
      log.push("c" + String(n) + " close");
    },
  };
}

// a service that reads the DataSource through a type of its own, query<T = Row>, and casts what it gives
type Row = Record<string, unknown>;
class PkPlanos {
  private readonly dataSource: DataSource;
  constructor(ds: DataSource) {
    this.dataSource = ds;
  }
  private get db(): { query<T = Row>(sql: string, binds?: unknown[]): Promise<T> } {
    return this.dataSource as unknown as { query<T = Row>(sql: string, binds?: unknown[]): Promise<T> };
  }
  async rows<T = Row>(sql: string, binds: unknown[] = []): Promise<T[]> {
    const r = (await this.db.query(sql, binds)) as T[] | T | null | undefined;
    if (Array.isArray(r)) {
      return r;
    }
    return r == null ? [] : [r];
  }
}

async function main(): Promise<void> {
  const pool: OraclePoolFunctions = {
    getConnection: async (): Promise<OracleConnectionFunctions> => connection(),
    close: async (): Promise<void> => {
      log.push("pool close");
    },
  };
  const ds = DataSource.withDriver({ type: "oracle" }, new OracleDriver(async (): Promise<OraclePoolFunctions> => pool));
  await ds.initialize();
  const repo = ds.getRepository<Plano>(Plano);
  const planos = await repo.find({ take: 2 });
  console.log(planos.map((p: Plano): string => String(p.id) + ":" + p.descricao).join(" "));
  console.log("count " + String(await repo.count()));
  const u = await repo.update({ id: 10 }, { descricao: "Diamante" });
  console.log("affected " + String(u.affected));

  const direct = await ds.query<{ CD_PLANO: number; DS_PLANO: string }[]>("SELECT CD_PLANO, DS_PLANO FROM TB_PLANO", []);
  console.log("direct " + String(direct.length));
  const pk = new PkPlanos(ds);
  const rs = await pk.rows<{ CD_PLANO: number; DS_PLANO: string }>("SELECT CD_PLANO, DS_PLANO FROM TB_PLANO WHERE CD_PLANO > :1", [1]);
  console.log("rows " + rs.map((r) => String(r.CD_PLANO) + "=" + r.DS_PLANO).join(" "));

  const qr = ds.createQueryRunner();
  await qr.startTransaction();
  await qr.query("UPDATE TB_PLANO SET DS_PLANO = :1 WHERE CD_PLANO = :2", ["x", 10]);
  await qr.manager.getRepository<Plano>(Plano).delete({ id: 11 });
  await qr.rollbackTransaction();
  await qr.release();
  await ds.destroy();
  for (const l of log) {
    console.log(l);
  }
}
main();
