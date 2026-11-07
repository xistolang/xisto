// node:sqlite: the same file runs on Node and on Xisto (SQLite compiled from source, linked only here).
import { DatabaseSync } from "node:sqlite";

interface Todo {
  id: int;
  title: string;
  done: int;
  note: string | null;
}

const db: DatabaseSync = new DatabaseSync(":memory:");
db.exec("create table todos (id integer primary key, title text not null, done integer not null default 0, note text)");
const insert = db.prepare("insert into todos (title, done, note) values (?, ?, ?)");
const r1 = insert.run("comprar pão", false, null);
const r2 = insert.run("estudar xisto", true, "urgente");
insert.run("lavar louça", false, null);
console.log(r1.changes, r1.lastInsertRowid, r2.lastInsertRowid);
const all: Todo[] = db.prepare("select id, title, done, note from todos order by id").all();
console.log(all.length, JSON.stringify(all));
const one: Todo | null = db.prepare("select id, title, done, note from todos where id = ?").get(2);
if (one !== null) {
  console.log(one.title, one.done, one.note);
}
const pending: Todo[] = db.prepare("select id, title, done, note from todos where done = ? and title like ?").all(0, "%a%");
console.log(pending.map((t) => t.title).join(" | "));
const upd = db.prepare("update todos set done = 1 where done = 0").run();
console.log(upd.changes);
interface Count {
  n: int;
  avg: number;
}
const c: Count | null = db.prepare("select count(*) as n, avg(id) as avg from todos").get();
if (c !== null) {
  console.log(c.n, c.avg);
}
try {
  db.exec("insert into nope values (1)");
} catch (e) {
  console.log(e.message.includes("no such table"));
}
db.close();
