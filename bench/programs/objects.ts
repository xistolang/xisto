// Objects and strings: allocate, link, read fields, build strings (refcounting and the allocator under load).
class Vec {
  x: number;
  y: number;
  label: string;
  constructor(x: number, y: number, label: string) {
    this.x = x;
    this.y = y;
    this.label = label;
  }
  add(o: Vec): Vec {
    return new Vec(this.x + o.x, this.y + o.y, this.label);
  }
}

function run(n: int): number {
  let acc: Vec = new Vec(0, 0, "acc");
  let total: int = 0;
  for (let i: int = 0; i < n; i++) {
    const v: Vec = new Vec(Number(i % 7), Number(i % 3), `v${i % 100}`);
    acc = acc.add(v);
    total += v.label.length;
  }
  return acc.x + acc.y + Number(total);
}

console.log("objects", run(3000000));
