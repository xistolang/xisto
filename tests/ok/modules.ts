// Modules: import/export across files; dependencies run first, once.
import { Point, midpoint, ORIGIN_LABEL, loads } from "./lib/geometry.ts";
import { fmt } from "./lib/format.ts";

const a: Point = new Point(3, 4);
const b: Point = new Point(-1, 2);
console.log(ORIGIN_LABEL, loads, a.norm(), fmt(midpoint(a, b)));
