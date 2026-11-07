// expect: 'r' is not a field of every member of Circle | Rect
interface Circle {
  kind: "circle";
  r: number;
}
interface Rect {
  kind: "rect";
  w: number;
}
function f(s: Circle | Rect): number {
  return s.r;
}
