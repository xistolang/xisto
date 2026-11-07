import { Point } from "./geometry.ts";

export function fmt(p: Point): string {
  return "(" + String(p.x) + ", " + String(p.y) + ")";
}
