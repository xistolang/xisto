// XB-140: an object literal where an interface with methods goes: methods written as m() { } (async too), arrows,
// `this` in a method (the object), and console.log shows the plain object
interface Io {
  query: (sql: string) => number;
  save(x: number): string;
  name: string;
}
let total = 0;
const io: Io = {
  query: (sql) => sql.length,
  save(x) { total += x; return `saved ${x}`; },
  name: "db",
};
console.log(io.query("select"), io.save(4), io.name, total);
function use(i: Io): string { return i.save(i.query("ab")); }
console.log(use(io));
interface Db {
  query(sql: string, binds: unknown[]): Promise<unknown[]>;
  close(): void;
}
class Real { async query(sql: string, binds: unknown[]): Promise<unknown[]> { return [sql, binds.length]; } }
const r = new Real();
const db: Db = { query: (s, b) => r.query(s, b), close: () => { console.log("closed"); } };
function mk(): Db { return { async query(s) { return [s]; }, close() { console.log("c2"); } }; }
interface Counter { n: number; inc(): number; label(p: string): string }
const c: Counter = {
  n: 5,
  inc() { return this.n + 1; },
  label(p) { return p + "!"; },
};
async function main() {
  console.log(await db.query("x", [1, 2]));
  db.close();
  const d2 = mk();
  console.log(await d2.query("y", []));
  d2.close();
  console.log(db, d2);
  console.log(c.inc(), c.label("x"), c);
}
main();
