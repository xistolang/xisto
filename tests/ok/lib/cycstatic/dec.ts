import { fmt } from "./util.ts";
export class Dec {
  static readonly ZERO = new Dec(0);
  v: number;
  constructor(v: number) { this.v = v; }
  show(): string { return fmt(this.v); }
  plus(o: Dec = Dec.ZERO): Dec { return new Dec(this.v + o.v); }
}
