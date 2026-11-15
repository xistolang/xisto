// XB-173/XB-195: a function inside a union (rxjs tap, class-validator message): narrowed, called, stored, printed, JSON
interface Observer<T> {
  next: (value: T) => void;
  error: (err: string) => void;
  complete: () => void;
}
function tap<T>(observerOrNext: Partial<Observer<T>> | ((value: T) => void), v: T): void {
  if (typeof observerOrNext === "function") {
    observerOrNext(v);
  } else {
    if (observerOrNext.next) observerOrNext.next(v);
    if (observerOrNext.complete) observerOrNext.complete();
  }
}
tap((v: number) => console.log("fn", v), 1);
tap({ next: (v: number) => console.log("obj next", v), complete: () => console.log("done") }, 2);

interface ValidationArguments { value: string; property: string }
interface Opts { message: string | ((args: ValidationArguments) => string) }
function msg(o: Opts, a: ValidationArguments): string {
  return typeof o.message === "function" ? o.message(a) : o.message;
}
console.log(msg({ message: "plain" }, { value: "v", property: "p" }));
console.log(msg({ message: (args) => `bad ${args.property}=${args.value}` }, { value: "v", property: "p" }));

let cb: (() => void) | null = null;
console.log(cb === null);
cb = () => console.log("cb");
if (cb) cb();

function named(n: number): number { return n * 2; }
let u: number | ((n: number) => number) = named;
console.log(u);
const arr: (string | ((n: number) => number))[] = ["a", named, (n: number) => n + 1];
console.log(arr);
for (const e of arr) {
  if (typeof e === "string") console.log("str", e);
  else console.log("fn", e(3));
}
const m = new Map<string, string | (() => string)>();
m.set("a", "x");
m.set("b", () => "y");
for (const [k, v] of m) console.log(k, typeof v === "function" ? v() : v);
console.log(m);
class Holder { h: string | ((n: number) => number) = "s"; }
const ho = new Holder();
console.log(ho);
ho.h = named;
console.log(ho);
console.log(JSON.stringify(ho));
console.log(JSON.stringify({ a: 1, f: u, s: "x" }));
const anon: string | (() => number) = () => 1;
console.log(anon);
