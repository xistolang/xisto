import { helper } from "./b.ts";
export class Dec {
  static readonly ZERO = Dec.make(0);
  static readonly LIST = [Dec.make(1), Dec.make(2)];
  static make(n: number) { return new Dec(n + helper()); }
  v: number;
  constructor(v: number) { this.v = v; }
}
