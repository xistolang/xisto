// Inheritance: extends, super(...), super.m(), overriding, abstract, static, instanceof, upcasts and downcasts.
abstract class Shape {
  static count: int = 0;
  readonly name: string;
  protected sides: int;

  constructor(name: string, sides: int) {
    this.name = name;
    this.sides = sides;
    Shape.count = Shape.count + 1;
  }

  abstract area(): number;

  describe(): string {
    return `${this.name} with ${this.sides} sides, area ${this.area().toFixed(2)}`;
  }

  kind(): string {
    return "shape";
  }

  static unit(): string {
    return "cm";
  }
}

class Rect extends Shape {
  w: number;
  h: number;
  constructor(w: number, h: number) {
    super("rect", 4);
    this.w = w;
    this.h = h;
  }
  area(): number {
    return this.w * this.h;
  }
  kind(): string {
    return "polygon/" + super.kind();
  }
}

class Square extends Rect {
  constructor(side: number) {
    super(side, side);
  }
  describe(): string {
    return "square: " + super.describe();
  }
}

class Circle extends Shape {
  private readonly r: number;
  constructor(r: number) {
    super("circle", 0);
    this.r = r;
  }
  area(): number {
    return Math.PI * this.r * this.r;
  }
}

class HttpError {
  readonly status: int;
  readonly message: string;
  constructor(status: int, message: string) {
    this.status = status;
    this.message = message;
  }
}
class NotFound extends HttpError {
  constructor(what: string) {
    super(404, `${what} not found`);
  }
}
class Teapot extends HttpError {}

const shapes: Shape[] = [new Rect(2, 3), new Square(2), new Circle(1)];
for (const s of shapes) {
  console.log(s.describe(), s.kind());
}
console.log("shapes made:", Shape.count, Shape.unit());

const first: Shape = shapes[0];
console.log(first instanceof Rect, first instanceof Square, shapes[1] instanceof Rect, shapes[2] instanceof Rect);
const sq = shapes[1] as Square;
console.log(sq.w, sq.area());

function total(list: Shape[]): number {
  let t = 0;
  for (const s of list) {
    t += s.area();
  }
  return t;
}
console.log(total(shapes).toFixed(3));

const errors: HttpError[] = [new NotFound("article"), new Teapot(418, "I'm a teapot")];
for (const e of errors) {
  console.log(e.status, e.message, e instanceof NotFound);
}
console.log(new Square(3));
