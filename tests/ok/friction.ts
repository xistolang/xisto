const m = new Map<string, string>();
m.set("a", "x");
m.set("e", "");
let calls = 0;
function name(n: int): string | null {
  calls++;
  return n > 0 ? `n${n}` : null;
}
function fallback(): string {
  calls += 100;
  return "fb";
}
console.log(m.get("a") || "none", m.get("zz") || "none", m.get("e") || "empty");
console.log(name(1) || fallback(), calls);
console.log(name(0) || fallback(), calls);
const parts: string[] = [];
for (let i: int = 0; i < 3; i++) {
  parts.push(name(i) || `missing${i}`);
}
console.log(parts.join(","), calls);

const q: string[] = ["b"];
q.unshift("a");
console.log(q.unshift("z"), q, q.shift(), q);
const n: number[] = [2, 3];
n.unshift(1);
console.log(n, n.shift(), n.length);
function lines(): string {
  return "my own lines";
}
let fetch = 1;
fetch++;
console.log(lines(), fetch);
// ?: unifies T with null, and two classes into their nearest common base
class Pet {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
}
class Cat extends Pet {}
class Dog extends Pet {}
function pick(n: int): string | null {
  return n > 1 ? `#${n}` : null;
}
function animal(c: boolean): Pet {
  const p = c ? new Cat("tom") : new Dog("rex");
  return p;
}
console.log(pick(2), pick(0), animal(true).name, animal(false).name);
// non-null: x! and (T | null) as T on values that are there
const found = m.get("a")!;
const also = m.get("a") as string;
console.log(found, also.length, pick(5)!.length);
