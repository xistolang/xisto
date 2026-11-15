// a module that imports the core's Span and describe under other names (XB-224: its own read as __local) and span-alt.ts's
// through a namespace, with its own Span, describe and TAG (XB-223)
import { Span as CoreSpan, describe as coreDescribe } from "./span-core.ts";
import * as alt from "./span-alt.ts";
export const TAG = "own2";
export class Span {
  tags: string[];
  constructor(tags: string[]) {
    this.tags = tags;
  }
  static make(id: number): Span {
    return new Span(["made", String(id)]);
  }
  kind(): string {
    return "own2 span " + this.tags.join(",");
  }
}
export function describe(s: Span): string {
  return "own2 describe " + s.kind();
}
export function run2(): string[] {
  const out: string[] = [];
  const cs = new CoreSpan("cs");
  const as = alt.Span.make(9);
  const mine = Span.make(4);
  out.push(coreDescribe(cs));
  out.push(alt.describe(as));
  out.push(describe(mine));
  out.push(alt.TAG + " " + TAG);
  const isCore = cs instanceof CoreSpan, isAlt = as instanceof alt.Span, isMine = mine instanceof Span;
  out.push(`${isCore} ${isAlt} ${isMine}`);
  out.push(JSON.stringify(cs) + " " + JSON.stringify(as) + " " + JSON.stringify(mine));
  return out;
}
export function both(): (CoreSpan | Span)[] {
  return [new CoreSpan("b1"), new Span(["b2"])];
}
