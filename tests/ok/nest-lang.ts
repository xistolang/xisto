// What the Nest packages lean on: object unions told apart by their keys, default type parameters, keywords as
// names, field types taken from the initializer, Class.name, int widening to number, process.pid and timers.
class Plain {
  id: string = "";
}
class ValueP {
  provide: string = "";
  useValue: string = "";
}
class ClassP {
  provide: string = "";
  useClass: string = "";
}
type P = ValueP | ClassP | Plain;

class Box<T> {
  items: T[] = [];
  add(x: T): Box<T> {
    this.items.push(x);
    return this;
  }
}

class Options {
  enum: string[] = [];
  default: string = "";
  new: boolean = false;
}

const names: Map<string, string> = new Map<string, string>();
names.set("app", "nest-lang");

function setting<T = string>(key: string, fallback: T | null = null): T | null {
  if (!names.has(key)) {
    return fallback;
  }
  return names.get(key) as unknown as T;
}

class Counter {
  label = "ticks";
  count = 0;
  ready = true;
  box = new Box<string>();
}

function total(xs: int[]): number {
  let n: number = 0;
  for (const x of xs) {
    n = x; // (an int assigned to a number widens)
  }
  return n;
}

const ps: P[] = [{ provide: "A", useValue: "1" }, { provide: "B", useClass: "Store" }, new Plain()];
console.log(ps.length, ps[2] instanceof Plain); // (each literal took the member whose keys it has)
const b = new Box<string>().add("x").add("y");
console.log(b.items.join(","));
const o = new Options();
o.enum = ["A", "B"];
o.default = "A";
console.log(o.enum.length, o.default, o.new);
const c = new Counter();
c.count = c.count + 2;
c.box.add("z");
console.log(c.label, c.count, c.ready, c.box.items.length);
const app = setting("app");
console.log(app === null ? "none" : app.toUpperCase(), setting("port", "3000"));
console.log(Counter.name, Options.name);
const len: int = "hello".length;
const asNumber: number = len;
console.log(asNumber / 2, total([1, 2, 3]));
console.log(process.pid > 0);

let ticks = 0;
let id = 0;
id = setInterval((): void => {
  ticks++;
  console.log("tick", ticks);
  if (ticks === 3) {
    clearInterval(id);
    console.log("stopped");
  }
}, 5);
const never = setTimeout((): void => {
  console.log("never");
}, 10);
clearTimeout(never);
setTimeout((): void => {
  console.log("later");
}, 40);
console.log("sync done");
