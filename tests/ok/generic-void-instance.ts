// A generic that keeps the callback's value to look at it before giving it back (a tracer's trace<T>(fn: () => T): T),
// instantiated with T = void: the call runs and the value is undefined, as in JavaScript (without --tsc-checked too)
function seen(v: unknown): string {
  return v === null || v === undefined ? "nothing" : typeof v;
}
function run<T>(fn: () => T): T {
  return fn();
}
function traced<T>(name: string, fn: (tag: string) => T): T {
  try {
    const v = run((): T => fn(name));
    console.log(name, "gave", seen(v));
    return v;
  } catch (err) {
    console.log(name, "threw", (err as Error).message);
    throw err;
  }
}
let count = 0;
traced("void", (tag) => {
  count++;
  console.log("in", tag);
});
console.log("count", count);
console.log("number", traced("num", (tag) => tag.length * 10));
console.log("text", traced("txt", (tag) => tag.toUpperCase()));
try {
  traced("bad", (tag): void => {
    throw new Error("no " + tag);
  });
} catch (err) {
  console.log("caught", (err as Error).message);
}
