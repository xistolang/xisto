// expect: field 'kind' must be "circle"
interface Circle {
  kind: "circle";
  r: number;
}
const c: Circle = { kind: "square", r: 1 };
