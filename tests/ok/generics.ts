// Generic functions and methods: inference from arguments, explicit type arguments, constraints, T in arrays,
// nullable and function types, instances per type.
interface Pair<A, B> {
  first: A;
  second: B;
}

function identity<T>(x: T): T {
  return x;
}

function firstOr<T>(list: T[], fallback: T): T {
  if (list.length === 0) {
    return fallback;
  }
  return list[0];
}

function mapAll<T, U>(list: T[], f: (x: T) => U): U[] {
  const out: U[] = [];
  for (const x of list) {
    out.push(f(x));
  }
  return out;
}

function pair<A, B>(a: A, b: B): Pair<A, B> {
  return { first: a, second: b };
}

function findOrNull<T>(list: T[], pred: (x: T) => boolean): T | null {
  for (const x of list) {
    if (pred(x)) {
      return x;
    }
  }
  return null;
}

class Animal {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
}
class Dog extends Animal {}

function loudest<T extends Animal>(list: T[]): string {
  let best = "";
  for (const a of list) {
    if (a.name.length > best.length) {
      best = a.name;
    }
  }
  return best.toUpperCase();
}

class Store {
  private data: Map<string, string> = new Map<string, string>();
  put<T>(key: string, value: T): void {
    this.data.set(key, JSON.stringify(value));
  }
  get<T>(key: string, fallback: T): T {
    if (!this.data.has(key)) {
      return fallback;
    }
    const raw = this.data.get(key) as string;
    const v: T = JSON.parse(raw);
    return v;
  }
}

console.log(identity(42), identity("hi"), identity(true), identity<int>(7));
console.log(firstOr([3, 1, 2], 0), firstOr([] as string[], "none"), firstOr(["a"], "z"));
console.log(mapAll([1, 2, 3], (x: number): string => `#${x}`), mapAll(["a", "bb"], (s: string): int => s.length));
const p = pair("x", 2);
console.log(p.first, p.second, JSON.stringify(pair(true, [1, 2])));
console.log(findOrNull([5, 8, 13], (x: number): boolean => x > 6), findOrNull(["a"], (s: string): boolean => s === "b"));
console.log(loudest([new Dog("rex"), new Dog("bobby")]), loudest([new Animal("cat")]));
const store = new Store();
store.put("n", 12);
store.put("tags", ["a", "b"]);
const n: number = store.get("n", 0);
const tags: string[] = store.get<string[]>("tags", []);
console.log(n, tags, store.get("missing", "default"));
