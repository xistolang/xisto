// Discriminated unions: 3 interfaces behind a kind, area by switch over 300 shapes, 100k rounds (30M dispatches).
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
      return 3.141592653589793 * s.r * s.r;
    case "rect":
      return s.w * s.h;
    case "tri":
      return (s.b * s.h) / 2;
  }
  return 0;
}
const shapes: Shape[] = [];
for (let i: int = 0; i < 300; i++) {
  if (i % 3 === 0) {
    shapes.push({ kind: "circle", r: Number(i % 5) });
  } else if (i % 3 === 1) {
    shapes.push({ kind: "rect", w: Number(i % 7), h: 2 });
  } else {
    shapes.push({ kind: "tri", b: Number(i % 11), h: 3 });
  }
}
let total = 0;
for (let round: int = 0; round < 100000; round++) {
  for (const s of shapes) {
    total += area(s);
  }
}
console.log(total.toFixed(2));
