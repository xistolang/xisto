// expect: cannot tell which member of Circle | Rect this object is
interface Circle {
  kind: "circle";
  r: number;
}
interface Rect {
  kind: "rect";
  w: number;
}
const s: Circle | Rect = { kind: "oval", r: 2 };
