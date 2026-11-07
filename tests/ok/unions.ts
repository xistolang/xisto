// Unions: string | number with typeof, classes with instanceof, discriminated unions of interfaces
// (string literal kinds) with if and switch, unions in arrays and returns, and null among the members.
interface Circle {
  kind: "circle";
  r: number;
}
interface Rect {
  kind: "rect";
  w: number;
  h: number;
}
interface Tri {
  kind: "tri";
  b: number;
  h: number;
}
type Shape = Circle | Rect | Tri;

function area(s: Shape): number {
  switch (s.kind) {
    case "circle":
      return Math.PI * s.r * s.r;
    case "rect":
      return s.w * s.h;
    case "tri":
      return (s.b * s.h) / 2;
  }
  return 0;
}

function label(s: Shape): string {
  if (s.kind === "rect") {
    return `rect ${s.w}x${s.h}`;
  }
  return s.kind;
}

function describe(v: string | number): string {
  if (typeof v === "string") {
    return `text(${v.length}): ${v.toUpperCase()}`;
  }
  return `number: ${(v * 2).toFixed(1)}`;
}

class Cat {
  meow(): string {
    return "meow";
  }
}
class Dog {
  bark(): string {
    return "woof";
  }
}
function speak(p: Cat | Dog): string {
  if (p instanceof Cat) {
    return p.meow();
  }
  return p.bark();
}

function parse(s: string): number | string | null {
  if (s.length === 0) {
    return null;
  }
  const n = Number(s);
  return isNaN(n) ? s : n;
}

const shapes: Shape[] = [{ kind: "circle", r: 1 }, { kind: "rect", w: 2, h: 3 }, { kind: "tri", b: 4, h: 5 }];
for (const s of shapes) {
  console.log(s.kind, area(s).toFixed(2), label(s));
}
console.log(describe("hello"), describe(21));
console.log(speak(new Cat()), speak(new Dog()));
const values: (string | number)[] = ["a", 1, "bb", 2.5];
let texts: int = 0;
let sum = 0;
for (const v of values) {
  if (typeof v === "number") {
    sum += v;
  } else {
    texts += v.length;
  }
}
console.log(texts, sum);
for (const s of ["42", "x", ""]) {
  const r = parse(s);
  if (r === null) {
    console.log("empty");
  } else if (typeof r === "number") {
    console.log("num", r + 1);
  } else {
    console.log("str", r);
  }
}
let current: string | number = "start";
console.log(describe(current));
current = 7;
console.log(describe(current));
