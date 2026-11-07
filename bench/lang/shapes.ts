// Virtual dispatch: 3 shapes behind a base class, area() summed over 30M calls (the call site sees all three).
abstract class Shape {
  abstract area(): number;
  scaled(k: number): number {
    return this.area() * k;
  }
}
class Rect extends Shape {
  w: number;
  h: number;
  constructor(w: number, h: number) {
    super();
    this.w = w;
    this.h = h;
  }
  area(): number {
    return this.w * this.h;
  }
}
class Circle extends Shape {
  r: number;
  constructor(r: number) {
    super();
    this.r = r;
  }
  area(): number {
    return 3.141592653589793 * this.r * this.r;
  }
}
class Tri extends Shape {
  b: number;
  h: number;
  constructor(b: number, h: number) {
    super();
    this.b = b;
    this.h = h;
  }
  area(): number {
    return (this.b * this.h) / 2;
  }
}
const shapes: Shape[] = [];
for (let i = 0; i < 300; i++) {
  if (i % 3 === 0) {
    shapes.push(new Rect(i % 7, 2));
  } else if (i % 3 === 1) {
    shapes.push(new Circle(i % 5));
  } else {
    shapes.push(new Tri(i % 11, 3));
  }
}
let total = 0;
for (let round = 0; round < 100000; round++) {
  for (const s of shapes) {
    total += s.scaled(1.0001);
  }
}
console.log(total.toFixed(2));
