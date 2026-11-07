// console.log with util.inspect's layout: cycles (<ref *1> ... [Circular *1]), depth 2 ([N]), lines past 80 columns,
// arrays of more than six entries in aligned columns, more than 100 items, Maps, empty containers at any depth
class N {
  name: string;
  next: N | null = null;
  kids: N[] = [];
  constructor(name: string) {
    this.name = name;
  }
}
const a = new N("a");
const b = new N("b");
a.next = b;
b.next = a;
console.log(a);
const r = new N("r");
const k = new N("k");
r.kids.push(k);
k.next = r;
console.log(r);
const d4 = new N("4");
const d3 = new N("3");
d3.next = d4;
const d2 = new N("2");
d2.next = d3;
const deep = new N("1");
deep.next = d2;
console.log(deep);
interface Cfg {
  aaaaaaaaaa: string;
  bbbbbbbbbb: string;
  c: string;
}
const o: Cfg = { aaaaaaaaaa: "xxxxxxxxxxxxxxxxxxxx", bbbbbbbbbb: "yyyyyyyyyyyyyyyyyyyy", c: "zzzzzzzzzzzzzzzzz" };
console.log(o);
const nums: int[] = [];
for (let i: int = 0; i < 30; i++) {
  nums.push(i * i);
}
console.log(nums);
const words: string[] = ["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta"];
console.log(words);
const big: int[] = [];
for (let i: int = 0; i < 130; i++) {
  big.push(i);
}
console.log(big);
const m = new Map<string, int[]>();
m.set("one", [1]);
m.set("two", [1, 2]);
console.log(m, [new N("x"), new N("y")]);
