// any: the dynamic path (MANIFESTO, "Ponto importante: any"). Same output as Node.
class Person {
  name: string = "Ana";
  age: number = 30;
  visits: int = 2;
  active: boolean = true;
  nick: string | null = null;
  score: number | null = null;
}
class Employee extends Person {
  company: string = "ACME";
}
interface Field {
  prop: string;
  date: boolean;
}
const FIELDS: Field[] = [
  { prop: "name", date: false },
  { prop: "age", date: false },
  { prop: "nick", date: false },
];

// rule 1: an object of a class seen as an any keeps its struct; fields by name
function describe(p: any): string {
  return `${p.name} (${p.age}) visits=${p.visits} active=${p.active} nick=${p.nick}`;
}
const ana = new Person();
console.log(describe(ana));
const emp = new Employee();
console.log(describe(emp), (emp as any).company);
function patch(target: any, changes: any): void {
  for (const f of FIELDS) {
    if (f.prop in changes) target[f.prop] = changes[f.prop];
  }
}
patch(ana, { age: 31, nick: "Aninha" });
console.log(ana.age, ana.nick, ana.name);
const values = FIELDS.map((f) => (ana as any)[f.prop]);
console.log(values);
console.log("nick" in (ana as any), "score" in (ana as any), "nope" in (ana as any));
(ana as any).score = 9.5;
console.log(ana.score);

// rule 2: an object made as an any is a Record
const where: any = {};
where.id = 7;
where["name"] = "x";
const key = "flag";
where[key] = true;
console.log(JSON.stringify(where));
console.log(where.id + 1, where.missing ?? "none", where.missing == null);
const made: any = { a: 1, b: "two", c: null };
console.log(JSON.stringify(made), made.a === 1, made.b !== "two");

// rule 3: values with their type, checked where they are used
function total(xs: any[]): number {
  let t = 0;
  for (const x of xs) t = t + Number(x);
  return t;
}
console.log(total([1, "2", 3.5]));
const rows: any[] = [{ CNT: 3 }, { CNT: 4 }];
const n: number = rows[0].CNT;
const cnt: int = rows[1].CNT;
console.log(n, cnt, rows.length, rows[0]?.CNT, rows[5]?.CNT ?? 0);
const s: any = "hello";
const t: any = 5;
console.log(s + "!", t * 2, t > 3, t - 1, `${s}/${t}`, String(t), s.length);
if (s) console.log("truthy string");
const empty: any = "";
if (!empty) console.log("empty is falsy");
const z: any = 0;
console.log(z || "zero", z ?? "not null");
const asStr: string = s;
console.log(asStr.toUpperCase());
const back = ana as any;
const p2: Person = back;
console.log(p2.name, typeof back, typeof s, typeof t);
