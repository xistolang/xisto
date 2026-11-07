// Classes: fields, constructor, methods, this, new, nested objects, refcounting.
class Point {
  x: number;
  y: number;
  label: string = "p";
  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }
  dist(other: Point): number {
    const dx: number = this.x - other.x;
    const dy: number = this.y - other.y;
    return Math.sqrt(dx * dx + dy * dy);
  }
  moved(dx: number, dy: number): Point {
    const p: Point = new Point(this.x + dx, this.y + dy);
    p.label = this.label + "_";
    return p;
  }
}

class Counter {
  count: int = 0;
  name: string;
  constructor(name: string) {
    this.name = name;
  }
  bump(): int {
    this.count++;
    return this.count;
  }
  describe(): string {
    return this.name + "=" + String(this.count);
  }
}

class Line {
  a: Point;
  b: Point;
  constructor(a: Point, b: Point) {
    this.a = a;
    this.b = b;
  }
  length(): number {
    return this.a.dist(this.b);
  }
}

const origin: Point = new Point(0, 0);
const p: Point = new Point(3, 4);
console.log(p.dist(origin), origin.dist(p), p.moved(1, 1).label, p.moved(1, 1).moved(1, 1).x);
console.log(p, origin.moved(0.5, 0));
const c: Counter = new Counter("hits");
c.bump();
c.bump();
console.log(c.bump(), c.describe(), c);
const line: Line = new Line(origin, p);
console.log(line.length(), line.a, line.b.label);
let cur: Point = origin;
for (let i: int = 0; i < 3; i++) {
  cur = cur.moved(1, 2);
}
console.log(cur.x, cur.y, cur.label);
function makeMany(n: int): int {
  let total: int = 0;
  for (let i: int = 0; i < n; i++) {
    const k: Counter = new Counter("k" + String(i));
    k.bump();
    total += k.count;
  }
  return total;
}
console.log(makeMany(1000));
