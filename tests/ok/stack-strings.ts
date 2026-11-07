// Strings that die with a stack object live in its frame (stackalloc.c): only when the field is read without
// being kept (.length, a comparison, a part of a template), in the function and in what the object is given to.
// Every other use must keep the string on the heap. The output is Node's.
class Tag {
  name: string;
  n: number;
  constructor(name: string, n: number) {
    this.name = name;
    this.n = n;
  }
  size(): number { return Number(this.name.length) + this.n; }
  label(): string { return this.name; }
  same(o: Tag): boolean { return this.name === o.name; }
}
const kept: string[] = [];
function keep(s: string): void { kept.push(s); }
function sizeOf(t: Tag): int { return t.name.length; }
function work(): number {
  let total = 0;
  const outer = new Tag("outer", 2); // a Tag that lives across the iterations: no region for Tag, so the stack applies
  for (let i = 0; i < 300; i++) {
    const a = new Tag(`a${i % 7}`, i);                // .length, a method that reads .length, a comparison
    const b = new Tag(`a${i % 5}`, 1);
    total += outer.n + Number(a.name.length + sizeOf(a)) + a.size() + (a.same(b) ? 1 : 0) + (a.name === "a3" ? 10 : 0);
    const msg = `${a.name}/${b.name}`;                 // parts of a template: copied
    total += Number(msg.length);
    const c = new Tag(`c${i}`, 0);
    kept.push(c.name);                                 // kept: on the heap
    const d = new Tag(`d${i}`, 0);
    const l = d.label();                               // returned by a method: on the heap
    if (i % 100 === 0) { kept.push(l); }
    const e = new Tag(`e${i}`, 0);
    keep(e.name);                                      // handed to a function that keeps it
    const big = new Tag(`${"x".repeat(80)}-${i}`, 0);   // longer than the frame's buffer: on the heap
    total += Number(big.name.length);
  }
  return total;
}
const total = work();
console.log(total, kept.length, kept[0], kept[1], kept[kept.length - 1], kept.slice(-3).join(","));
