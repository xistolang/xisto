// A null test on a field narrows the field, as TypeScript does: o.f !== null, then o.f is the value (read with a
// run-time check, so a call that empties the field in between stops with an error instead of crashing)
interface Opts { name: string; verbose?: boolean; retries?: int; ratio?: number; tag?: string; }
class Node2 {
  next: Node2 | null = null;
  val: int;
  label: string | null = null;
  constructor(v: int) { this.val = v; }
  describe(): string {
    if (this.label !== null) { return `${this.val}:${this.label.toUpperCase()}`; }
    return `${this.val}`;
  }
  sumChain(): int {
    let s: int = this.val;
    if (this.next !== null && this.next.val > 0) { s += this.next.sumChain(); }
    return s;
  }
}
function show(o: Opts): string {
  let out = o.name;
  if (o.verbose !== null && o.verbose) { out += " verbose"; }
  if (o.retries !== null) { out += ` retries=${o.retries + 1}`; }
  if (o.ratio === null) { return out + " (no ratio)"; }
  out += ` ratio=${o.ratio * 2}`;
  if (o.tag !== null) {
    out += ` tag=${o.tag.length}`;
    o.tag = "changed";
    out += ` now=${o.tag}`;
  }
  return out;
}
console.log(show({ name: "a", verbose: null, retries: null, ratio: null, tag: null })); // (xisto has no undefined: null is the absent value)
console.log(show({ name: "b", verbose: true, retries: 3, ratio: 0.5, tag: "t" }));
console.log(show({ name: "c", verbose: false, retries: null, ratio: 2, tag: null }));
const n = new Node2(1);
n.next = new Node2(2);
n.next.next = new Node2(3);
n.label = "one";
console.log(n.describe(), n.next.describe(), n.sumChain());

class Box { v: string | null = "x"; }
function clear(b: Box): int { b.v = null; return 0; }
const b = new Box();
try {
  if (b.v !== null) {
    const k = clear(b);
    console.log(b.v.length + k);
  }
} catch (e) {
  console.log("caught:", (e as Error).message.length > 0);
}
