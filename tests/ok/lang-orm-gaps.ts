// What an ORM package needed from the language: keywords as property names, abstract methods with parameters
// called through the base, object | null fields, array literals where an object or a union goes, JSON.parse into
// an object
interface ColumnOptions {
  name?: string;
  default?: string | number | boolean;
  delete?: boolean;
}
const col: ColumnOptions = { name: "price", default: 0, delete: true };
const dflt = col.default;
console.log((typeof dflt === "number" ? "number " + String(dflt) : "other") + " " + String(col.delete ?? false));

abstract class Driver {
  abstract run(sql: string, params: int[]): string;
  abstract runAsync(sql: string): Promise<string>;
}
class Fake extends Driver {
  run(sql: string, params: int[]): string {
    return sql + " with " + String(params.length);
  }
  async runAsync(sql: string): Promise<string> {
    return "async " + sql;
  }
}

interface FindOptions {
  where?: object;
  take?: int;
}
function describe(o: FindOptions): string {
  const w = o.where;
  return (w == null ? "no where" : JSON.stringify(w)) + ", take " + String(o.take ?? 0);
}

function pick(x: string | string[]): string {
  if (typeof x === "string") {
    return x;
  }
  return x.join("+");
}

function many(rows: object): string {
  return JSON.stringify(rows);
}

async function main(): Promise<void> {
  const d: Driver = new Fake();
  console.log(d.run("select", [1, 2]));
  console.log(await d.runAsync("insert"));
  console.log(describe({ where: { id: 1, tags: ["a", "b"] }, take: 5 }));
  console.log(describe({ take: 2 }));
  const parsed: FindOptions = JSON.parse('{"where":{"q":"x"},"take":3}');
  console.log(describe(parsed));
  console.log(pick("one") + " " + pick(["a", "b", "c"]));
  console.log(many([{ id: 1 }, { id: 2, name: "two" }]));
  const raw: object = JSON.parse('[{"ID":7}]');
  console.log(JSON.stringify(raw));
  try {
    const bad: object = JSON.parse("{oops");
    console.log(JSON.stringify(bad));
  } catch (e) {
    console.log("invalid JSON");
  }
}
main();
