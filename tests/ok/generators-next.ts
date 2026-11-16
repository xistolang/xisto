// Generators driven by hand: next(v) gives v to the yield the generator waits at (const v = yield x; v = yield x;
// numbers, strings, objects), next() and return(v) results printed as on Node (value undefined once done), and
// generators spread into an array, a rest parameter and Math.max.
function* acc(): Generator<number, void, number> {
  let total = 0;
  while (total < 10) {
    const v: number = yield total;
    total += v;
  }
}
const g = acc();
console.log(g.next(0));
console.log(g.next(3));
console.log(g.next(4));
console.log(g.next(5));
console.log(g.next(1));

function* talk(): Generator<string, void, string> {
  let name = "nobody";
  name = yield "who?";
  const mood = yield `hi ${name}`;
  yield `${name} is ${mood}`;
}
const t = talk();
console.log(t.next().value, t.next("ann").value, t.next("happy").value, t.next("x").done);

function* nums(): Generator<number> { yield 1; yield 2; yield 3; }
const n = nums();
console.log(n.next(), n.return(9), n.next());
const n2 = nums();
const first = n2.next();
console.log(first.value + 1, first.done, [...n2]);
const { value, done } = nums().next();
console.log(value, done);
function sum(...xs: number[]): number { let s = 0; for (const x of xs) s += x; return s; }
console.log(sum(...nums()), Math.max(...nums()));
class Tree {
  kids: Tree[] = [];
  label: string;
  constructor(label: string) { this.label = label; }
  add(t: Tree): Tree { this.kids.push(t); return this; }
  *walk(): Generator<string> {
    yield this.label;
    for (const k of this.kids) yield* k.walk();
  }
}
const tree = new Tree("a").add(new Tree("b").add(new Tree("c"))).add(new Tree("d"));
const w = tree.walk();
console.log(w.next().value, w.next().value, [...w]);
function* objs(): Generator<Tree, void, Tree> {
  let cur = new Tree("start");
  while (cur.label !== "stop") {
    cur = yield cur;
  }
}
const o = objs();
console.log(o.next().value.label, o.next(new Tree("mid")).value.label, o.next(new Tree("stop")).done);
