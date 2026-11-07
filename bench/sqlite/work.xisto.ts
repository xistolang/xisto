// SQLite workload: 200k inserts in one transaction, 2k point reads, one full scan into typed rows.
import { DatabaseSync } from "node:sqlite";

interface Row {
  id: int;
  name: string;
  score: number;
}

const db: DatabaseSync = new DatabaseSync(":memory:");
db.exec("create table t (id integer primary key, name text, score real)");
const ins = db.prepare("insert into t (name, score) values (?, ?)");
db.exec("begin");
for (let i: int = 0; i < 200000; i++) {
  ins.run(`name ${i}`, Number(i) * 0.5);
}
db.exec("commit");
const one = db.prepare("select id, name, score from t where id = ?");
let sum: number = 0;
for (let i: int = 1; i <= 2000; i++) {
  const r: Row | null = one.get(i * 97);
  if (r !== null) {
    sum += r.score;
  }
}
const all: Row[] = db.prepare("select id, name, score from t where score > ?").all(1000);
console.log(sum, all.length);
