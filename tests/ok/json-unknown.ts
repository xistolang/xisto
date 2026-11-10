// JSON.stringify of an unknown, and of classes with an unknown field: written by the value's type at run time
// (a string, a number, a boolean, an array, an object of a class); s + "!" returned where unknown goes stays a string
class Item {
  id: int = 1;
  name: string = "a";
  tags: string[] = ["x", "y"];
}
class Envelope {
  success: boolean = true;
  data: unknown = null;
  constructor(data: unknown) {
    this.data = data;
  }
}
const vals: unknown[] = [null, 1, 2.5, "s\"q", true, [1, 2], ["a", "b"], new Item(), [new Item()]];
for (const v of vals) console.log(JSON.stringify(v));
console.log(JSON.stringify(new Envelope(new Envelope([3.5, 4]))));
const n: unknown = 7;
console.log(JSON.stringify(new Envelope(n)));
function ap(fn: (v: unknown) => unknown, v: unknown): unknown {
  return fn(v);
}
console.log(ap((s) => (s as string) + "y", "x") as string);
function g(s: string): unknown {
  return s + "!";
}
console.log(JSON.stringify(g("a")));
