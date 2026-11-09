// re-exported by ./index.ts by name
export class Square {
  s: number;
  constructor(s: number) { this.s = s; }
  area(): number { return this.s * this.s; }
}
export function notReexported(): number { return 7; }
