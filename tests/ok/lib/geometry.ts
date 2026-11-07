// A module: exported class, function and constant; a private helper.
export const ORIGIN_LABEL: string = "origin";

export class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }
  norm(): number {
    return Math.sqrt(sq(this.x) + sq(this.y));
  }
}

function sq(v: number): number {
  return v * v;
}

export function midpoint(a: Point, b: Point): Point {
  return new Point((a.x + b.x) / 2, (a.y + b.y) / 2);
}

export let loads: int = 0;
loads++;
console.log("geometry loaded");
