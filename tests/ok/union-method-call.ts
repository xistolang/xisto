// XB-172: a method called on a union of classes (each has it): dispatched on the object's class, the receiver and the
// arguments evaluated once and in order, awaited or not
class Runner {
  id = 1;
  async query(sql: string, params?: unknown[]): Promise<number> { return sql.length + (params ? params.length : 0); }
  label(p: string): string { return p + "runner"; }
}
class Source {
  name = "ds";
  async query(sql: string, params?: unknown[]): Promise<number> { return 100 + sql.length; }
  label(p: string): string { return p + "source"; }
}
let n = 0;
function pick(): Runner | Source { n++; return n % 2 ? new Runner() : new Source(); }
function arg(): string { console.log("arg", n); return "q" + n; }
class Svc {
  via: Runner | Source = new Source();
  async go(): Promise<number> { return await this.via.query(arg()); }
}
async function run(via: Runner | Source, q: string): Promise<number> {
  const r = await via.query(q, [1, 2]);
  const s = await via.query(q);
  return r + s;
}
async function main() {
  console.log(await run(new Runner(), "abc"), await run(new Source(), "abc"));
  console.log(pick().label(arg()), pick().label("-"));
  console.log(await pick().query(arg()), await pick().query("lit", [1]));
  console.log(await new Svc().go());
  const xs: (Runner | Source)[] = [new Runner(), new Source()];
  for (let i = 0; i < xs.length; i++) console.log(xs[i].label("#"), await xs[i].query("z"));
}
main();
