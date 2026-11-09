// typeorm against a live PostgreSQL (tests/typeorm/run.sh points it at the xisto-pg container): repository,
// query builder, raw queries and transactions on a table of its own, dropped at the end
import { Column, DataSource, Entity, In, LessThanOrEqual, PrimaryColumn } from "typeorm";

@Entity("typeorm_test_item")
class Item {
  @PrimaryColumn({ name: "id" })
  id: int;
  @Column({ name: "item_name" })
  name: string;
  @Column({ name: "price", type: "numeric" })
  price: number;
  @Column({ name: "active" })
  active: boolean;
  @Column({ name: "note", nullable: true })
  note: string | null;
}

interface Total {
  total: string;
}

async function main(): Promise<void> {
  const port = Number(process.env.get("PGPORT_TEST") ?? "55432");
  const ds = new DataSource({ type: "postgres", host: "127.0.0.1", port: Math.trunc(port), username: "xisto", password: "xisto", database: "conduit" });
  await ds.initialize();
  await ds.query("DROP TABLE IF EXISTS typeorm_test_item");
  await ds.query("CREATE TABLE typeorm_test_item (id int PRIMARY KEY, item_name text NOT NULL, price numeric(10,2) NOT NULL, active boolean NOT NULL, note text)");
  const repo = ds.getRepository<Item>(Item);

  const a: Item = { id: 1, name: "apple", price: 2.5, active: true, note: null };
  const b: Item = { id: 2, name: "bread", price: 7.25, active: true, note: "fresh" };
  const c: Item = { id: 3, name: "cheese", price: 30, active: false, note: null };
  await repo.save(a);
  await repo.save(b);
  await repo.save(c);
  b.price = 8;
  await repo.save(b); // the row exists: UPDATE
  console.log("count " + String(await repo.count()));

  const cheap = await repo.find({ where: { active: true, price: LessThanOrEqual(10) }, order: { name: "DESC" } });
  for (const i of cheap) {
    console.log(i.name + " " + String(i.price) + " " + String(i.active) + " " + (i.note ?? "-"));
  }
  const one = await repo.findOneBy({ id: 3 });
  console.log(one === null ? "none" : one.name + " active=" + String(one.active));
  const none = await repo.findOneBy({ id: 99 });
  console.log(none === null ? "no 99" : "found 99");
  const some = await repo.find({ where: { id: In([1, 3]) }, order: { id: "ASC" }, take: 1, skip: 1 });
  console.log("page " + some[0].name);

  const upd = await repo.update({ id: 1 }, { note: "red" });
  console.log("updated " + String(upd.affected));
  const qb = await repo.createQueryBuilder("i").where("i.price > :min", { min: 5 }).orderBy("i.price", "ASC").getMany();
  console.log("qb " + qb.map((i: Item): string => i.name).join(","));
  console.log("qb count " + String(await repo.createQueryBuilder("i").where("i.active = :on", { on: true }).getCount()));
  const raw: Total[] = await ds.query("SELECT SUM(price)::text AS total FROM typeorm_test_item WHERE id = ANY($1::int[])", ["{1,2}"]);
  console.log("sum " + raw[0].total);

  const qr = ds.createQueryRunner();
  await qr.connect();
  await qr.startTransaction();
  await qr.query("UPDATE typeorm_test_item SET item_name = 'kept' WHERE id = 1", []);
  await qr.commitTransaction();
  await qr.startTransaction();
  await qr.query("UPDATE typeorm_test_item SET item_name = 'lost' WHERE id = 2", []);
  await qr.rollbackTransaction();
  await qr.release();
  const after = await repo.find({ order: { id: "ASC" } });
  console.log("after tx " + after.map((i: Item): string => i.name).join(","));

  try {
    await ds.transaction(async (m) => {
      await m.query("UPDATE typeorm_test_item SET item_name = 'never' WHERE id = 3", []);
      throw new Error("abort");
    });
  } catch (e) {
    console.log("transaction rolled back: " + e.message);
  }
  const three = await repo.findOneBy({ id: 3 });
  console.log("3 is " + (three === null ? "gone" : three.name));

  const del = await repo.delete({ id: In([1, 2]) });
  console.log("deleted " + String(del.affected) + ", left " + String(await repo.count()));
  try {
    await ds.query("SELECT * FROM typeorm_no_such_table", []);
  } catch (e) {
    console.log("error: " + (e.message.indexOf("typeorm_no_such_table") >= 0 ? "relation missing" : e.message));
  }
  await ds.query("DROP TABLE typeorm_test_item");
  await ds.destroy();
}
main();
