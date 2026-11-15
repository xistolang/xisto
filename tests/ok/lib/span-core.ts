export function startSpan(name: string): string {
  return "core:" + name;
}
export const VERSION = "core-1";
export class Span {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
  kind(): string {
    return "core span " + this.name;
  }
}
export function describe(s: Span): string {
  return "core describe " + s.kind();
}
