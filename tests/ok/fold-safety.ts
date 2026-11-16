// Literal arguments of a function called once (src/fold.c) where the function is reached in other ways too: each
// either keeps its reads or prints what Node prints.
import { scale } from "./lib/fold-scale.ts";

// (a) a function passed as a value, or held in a field, then called with other arguments
function tag(kind: string): string {
  return kind === "a" ? "A" : `other ${kind}`;
}
const f: (k: string) => string = tag;
class Holder {
  fn: (k: string) => string;
  constructor(fn: (k: string) => string) {
    this.fn = fn;
  }
}
const h = new Holder(tag);
console.log(tag("a"), f("b"), h.fn("c"));

// (b) exported from another module and called once with a literal there
console.log(scale("half", 9));

// (c) recursion: one call from outside, the others from itself
function countdown(n: number, label: string): string {
  if (n <= 0) return label;
  return countdown(n - 1, `${label}.`);
}
console.log(countdown(3, "go"));

// (d) one call site inside a closure that runs many times
function step(mode: string, x: number): number {
  return mode === "inc" ? x + 1 : x - 1;
}
let total = 0;
[1, 2, 3, 4].forEach((v: number): void => {
  total += step("inc", v);
});
console.log(total);

// (e) default parameters, left out and given
function greet(name: string = "world", punct: string | null = null): string {
  return `hello ${name}${punct ?? "!"}`;
}
function greetTwo(a: string, b: number = 7): string {
  return `${a}${b > 5 ? "+" : "-"}`;
}
console.log(greet(), greetTwo("x", 2));

// a once-called function whose literal object is also changed inside: its reads stay
interface Cfg {
  n?: number;
}
function mutate(c: Cfg): number {
  c.n = 5;
  return c.n ?? 0;
}
console.log(mutate({ n: 1 }));
