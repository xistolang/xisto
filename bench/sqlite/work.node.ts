import { DatabaseSync } from "node:sqlite";
const db = new DatabaseSync(":memory:");
db.exec("create table t (id integer primary key, name text, score real)");
const ins = db.prepare("insert into t (name, score) values (?, ?)");
db.exec("begin");
for (let i = 0; i < 200000; i++) ins.run(`name ${i}`, i * 0.5);
db.exec("commit");
const one = db.prepare("select id, name, score from t where id = ?");
let sum = 0;
for (let i = 1; i <= 2000; i++) {
  const r = one.get(i * 97) as { score: number } | undefined;
  if (r !== undefined) sum += r.score;
}
const all = db.prepare("select id, name, score from t where score > ?").all(1000);
console.log(sum, all.length);
