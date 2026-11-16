import { Dec } from "./dec.ts";
export function fmt(n: number): string { return "D" + n; }
export function zero() { return Dec.ZERO; }
export class Holder { d = Dec.ZERO; get() { return this.d.plus(); } }
