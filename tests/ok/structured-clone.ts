// structuredClone as Node's: a class's copy is a plain object with its own fields (no class name, not an instance),
// shared references stay shared and a cycle is copied as a cycle, a function inside is a DataCloneError one can catch
class Point {
  x: number;
  tags: string[];
  when: Date;
  constructor(x: number, tags: string[]) {
    this.x = x;
    this.tags = tags;
    this.when = new Date(0);
  }
  sum(): number {
    return this.x;
  }
}
class Point3 extends Point {
  extra: Map<string, number> = new Map<string, number>([["k", 2]]);
}
class Empty {}
const p = new Point(1, ["a"]);
const q = structuredClone(p);
q.tags.push("b");
console.log(p.tags.length, q.tags.length, q.x, q instanceof Point, p instanceof Point);
console.log(q);
const q3 = structuredClone(new Point3(2, []));
console.log(q3, q3 instanceof Point3, q3 instanceof Point);
console.log(structuredClone(new Empty()));
console.log(JSON.stringify(q), Object.keys(q3));
console.log(structuredClone([1, 2, 3]), structuredClone(["a", "b"]), structuredClone(new Set<number>([1, 2])));
const m = structuredClone(new Map<string, number[]>([["a", [1]]]));
console.log(m, m.get("a"));
const d = structuredClone(new Date(5));
console.log(d.getTime(), d instanceof Date);
const rec: Record<string, number> = { a: 1, b: 2 };
console.log(structuredClone(rec));
// shared references stay shared
const shared = [1, 2];
const pair = structuredClone([shared, shared]);
pair[0].push(3);
console.log(pair, shared);
const ps = [p, p];
const ps2 = structuredClone(ps);
ps2[0].x = 9;
console.log(ps2[1].x, p.x);
// a cycle
class Link {
  name: string;
  next: Link | null = null;
  constructor(name: string) {
    this.name = name;
  }
}
const a = new Link("a");
const b = new Link("b");
a.next = b;
b.next = a;
const a2 = structuredClone(a);
console.log(a2.name, a2.next?.name, a2.next?.next === a2, a2.next?.next === a);
console.log(a2);
interface Tree {
  name: string;
  kids: Tree[];
}
const tree: Tree = { name: "r", kids: [{ name: "c", kids: [] }] };
const t2 = structuredClone(tree);
t2.kids[0].name = "changed";
console.log(JSON.stringify(t2), tree.kids[0].name);
interface WithFn {
  n: number;
  f: () => number;
}
const wf: WithFn = { n: 1, f: (): number => 1 };
try {
  structuredClone(wf);
  console.log("no error");
} catch (e) {
  console.log("caught", (e as Error).name);
}
console.log(structuredClone("s"), structuredClone(3), structuredClone(null));
