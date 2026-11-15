// XB-215: a module variable passed to a function that assigns it: the value the callee was given stays alive for the
// call (it was freed under it: garbage, a use after free)
let buf = "x".repeat(300);
function g(x: string): void {
  buf += "a";
  console.log(x.length, buf.length);
}
g(buf);
let list: number[] = [1, 2, 3];
function swap(xs: number[]): number {
  list = [9];
  let s = 0;
  for (const v of xs) s += v;
  return s + list.length;
}
console.log(swap(list), list.join(","));
class Node2 { v: string; constructor(v: string) { this.v = v; } }
let cur: Node2 = new Node2("first".repeat(40));
class Walker {
  step(n: Node2): string { cur = new Node2("second"); return n.v.length + " " + cur.v; }
}
console.log(new Walker().step(cur));
const replace = (s: string): number => { buf = "y"; return s.length; };
console.log(replace(buf), buf);
let names: string[] = ["a", "b"];
const seen: string[] = [];
names.forEach((n: string): void => { names = []; seen.push(n); });
console.log(seen.join(","), names.length);
class Keeper {
  text: string;
  constructor(t: string) { buf = "z"; this.text = t + "!"; }
}
buf = "w".repeat(50);
console.log(new Keeper(buf).text.length, buf);
// in a loop, many calls: each held value is let go
let acc = "";
function grow(s: string): void { acc = acc + "."; if (s.length > 1000000) console.log("?"); }
for (let i = 0; i < 1000; i++) grow(acc);
console.log(acc.length);
