// Arrow functions (capture by value), array methods, for...of.
class Todo {
  id: int;
  title: string;
  done: boolean;
  constructor(id: int, title: string, done: boolean) {
    this.id = id;
    this.title = title;
    this.done = done;
  }
}

class Store {
  items: Todo[] = [];
  prefix: string = "#";
  add(title: string, done: boolean): void {
    this.items.push(new Todo(this.items.length + 1, title, done));
  }
  titles(): string[] {
    return this.items.map((t) => this.prefix + t.title);
  }
  pending(): Todo[] {
    return this.items.filter((t: Todo): boolean => !t.done);
  }
  byId(id: int): Todo | null {
    return this.items.find((t) => t.id === id);
  }
}

const store: Store = new Store();
store.add("one", false);
store.add("two", true);
store.add("three", false);
console.log(store.titles(), store.pending().length, store.pending().map((t) => t.id));
const found: Todo | null = store.byId(2);
console.log(found === null ? "none" : found.title, store.pending().length);

const nums: number[] = [3, 1, 2];
const doubled: number[] = nums.map((x) => x * 2);
const big: number[] = nums.filter((x) => x >= 2);
const labels: string[] = nums.map((x, i) => `${i}:${x}`);
console.log(doubled, big, labels, nums.some((x) => x > 2), nums.every((x) => x > 2), nums.findIndex((x) => x === 2), nums.indexOf(1), nums.includes(5));
const factor: number = 10;
const scaled: number[] = nums.map((x) => x * factor);
console.log(scaled, nums.join("-"), labels.join(), [1, 2, 3].join(", "));
nums.sort((a, b) => a - b);
console.log(nums, nums.slice(1), nums.slice(0, 2), nums.slice(-1), nums.reverse());
const words: string[] = ["pear", "fig", "apple"];
words.sort((a, b) => a.length - b.length);
console.log(words, words.map((w) => w.toUpperCase()), words.indexOf("fig"), words.includes("kiwi"));
let total: number = 0;
for (const n of nums) {
  total += n;
}
for (const w of words) {
  if (w === "fig") {
    continue;
  }
  console.log(w.length, w);
}
console.log(total);
nums.forEach((x) => {
  console.log("each", x);
});
const twice = (x: number): number => x * 2;
const apply = (f: (v: number) => number, v: number): number => f(v);
console.log(twice(21), apply(twice, 4), apply((v) => v + 1, 4));
const counts: int[] = [5, 3, 9];
const sum = (xs: int[]): int => {
  let s: int = 0;
  for (const x of xs) {
    s += x;
  }
  return s;
};
console.log(sum(counts), counts.map((c) => c % 2 === 0), counts.sort((a, b) => a - b));
async function later(): Promise<int> {
  const inc = async (n: int): Promise<int> => n + 1;
  return await inc(41);
}
console.log("later", await later());
