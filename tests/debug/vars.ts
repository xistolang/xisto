enum Color { Red = "red", Green = "green" }
interface Point { x: int; y: int; }
class Shape {
  name: string;
  points: Point[];
  color: Color;
  parent: Shape | null = null;
  constructor(name: string, color: Color) {
    this.name = name;
    this.points = [];
    this.color = color;
  }
  add(x: int, y: int): int {
    const p: Point = { x: x, y: y };
    this.points.push(p);
    return this.points.length;
  }
}

const limit: int = 3;
let ratio: number = 0.5;
const tags: string[] = ["a", "b"];
const nothing: string | null = null;
function build(n: int): Shape {
  const s = new Shape("tri", Color.Green);
  for (let i: int = 0; i < n; i++) {
    const count = s.add(i, i * 2);
    ratio = ratio + Number(count);
  }
  for (const t of tags) {
    s.name = s.name + t;
  }
  try {
    throw new Error("nope");
  } catch (e) {
    s.name = s.name + "!";
  }
  return s;
}
const shape = build(limit);
console.log(shape.name, ratio);
