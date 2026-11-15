// a module that declares its own Span and describe and also reads span-core.ts's through a namespace (XB-223)
import * as core from "./span-core.ts";
export class Span {
  depth: number;
  constructor(depth: number) {
    this.depth = depth;
  }
  kind(): string {
    return `own span depth ${this.depth}`;
  }
}
export function describe(s: Span): string {
  return "own describe " + s.kind();
}
export function run(): string[] {
  const c2 = new core.Span("c2");
  const mine = new Span(3);
  const out: string[] = [];
  out.push(core.describe(c2));
  out.push(describe(mine));
  out.push(core.startSpan("own"));
  out.push(String(c2 instanceof core.Span) + " " + String(mine instanceof Span));
  out.push(JSON.stringify(c2) + " " + JSON.stringify(mine));
  return out;
}
export function makeCore(name: string): core.Span {
  return new core.Span(name);
}
