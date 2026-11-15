// a second module with a class Span and a function describe, as span-core.ts has (XB-223)
export const TAG = "alt";
export class Span {
  id: number;
  label: string;
  constructor(id: number, label: string) {
    this.id = id;
    this.label = label;
  }
  static make(id: number): Span {
    return new Span(id, "made");
  }
  kind(): string {
    return `alt span #${this.id} ${this.label}`;
  }
}
export function describe(s: Span): string {
  return "alt describe " + s.kind();
}
