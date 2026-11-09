export class Circle {
  r: number;
  constructor(r: number) { this.r = r; }
  area(): number { return 3 * this.r * this.r; }
}
