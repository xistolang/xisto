// re-exported by ./index.ts with export * from
export class Circle {
  r: number;
  constructor(r: number) { this.r = r; }
  area(): number { return 3 * this.r * this.r; }
}
export function unitCircle(): Circle { return new Circle(1); }
export const CIRCLE_SIDES: number = 0;
