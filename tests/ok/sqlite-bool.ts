// Typed rows: a boolean field reads SQLite's 0/1; a missing row is null (Node gives undefined, so this file has a gabarito).
import { DatabaseSync } from "node:sqlite";

interface Flag {
  name: string;
  on: boolean;
}

const db: DatabaseSync = new DatabaseSync(":memory:");
db.exec("create table flags (name text, on_ integer)");
const ins = db.prepare("insert into flags values (?, ?)");
ins.run("dark", true);
ins.run("beta", false);
const flags: Flag[] = db.prepare("select name, on_ as \"on\" from flags").all();
console.log(flags);
const missing: Flag | null = db.prepare("select name, on_ as \"on\" from flags where name = ?").get("nope");
console.log(missing === null, flags[0].on, flags[1].on);
