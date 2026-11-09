// export * from, export { a } from, export type { A } from, export { a, b }; and import type: a barrel's names
// are imported from the barrel (or a barrel of it) as if they were declared there
import { Circle, Square, unitCircle, CIRCLE_SIDES, kindOf, KIND_LIMIT } from "./lib/barrel/index.ts";
import { Circle as Round } from "./lib/barrel/outer.ts";
import type { Square as Box } from "./lib/barrel/square.ts";

function boxArea(b: Box): number { return b.area(); }

const c = new Circle(2);
console.log(c.area(), new Square(3).area(), unitCircle().area(), CIRCLE_SIDES);
console.log(kindOf(1), kindOf(-1), KIND_LIMIT);
console.log(new Round(4).area(), boxArea(new Square(5)));
