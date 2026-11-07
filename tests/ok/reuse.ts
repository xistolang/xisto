// x = x.m(...) where m returns a new object of x's class: rewritten in place when x holds the only
// reference (XB-036). Every other reference must keep seeing the old object.
class P {
  x: number;
  y: number;
  tag: string;
  constructor(x: number, y: number, tag: string) {
    this.x = x;
    this.y = y;
    this.tag = tag;
  }
  moved(dx: number): P { return new P(this.x + dx, this.y, this.tag); }
  renamed(t: string): P { return new P(this.x, this.y, t); }
  plus(o: P): P { return new P(this.x + o.x, this.y + o.y, `${this.tag}+${o.tag}`); }
  swapped(): P { return new P(this.y, this.x, this.tag); }
}
let a = new P(1, 2, "a");
const keep = a;                 // a second reference: a gets a new object, keep stays as it was
a = a.moved(10);
console.log(keep.x, a.x, keep === a);
let b = new P(1, 2, "b");
for (let i: int = 0; i < 5; i++) { b = b.moved(Number(i)); }
b = b.renamed(`b${b.x}`);
console.log(b.x, b.tag);
let c = new P(3, 4, "c");
c = c.plus(c);                  // itself as the argument: read before written
c = c.swapped();                // fields read from the old values, not from the half-written ones
console.log(c.x, c.y, c.tag);
const list: P[] = [];
let d = new P(0, 0, "d");
for (let i: int = 0; i < 3; i++) {
  d = d.moved(1);
  if (i === 1) { list.push(d); } // kept in the array: the next update makes a new object
}
console.log(list[0].x, d.x, list.length);
