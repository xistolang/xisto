// The iteration protocol: a class's [Symbol.iterator]() returning an iterator object (next(), done/value; one that
// is its own iterator too), [Symbol.asyncIterator]() with for await, and generators and iterable classes where
// an array goes: [...g()], Array.from(g()), Array.from(g(), f), Math.max(...g()), new Set(g()).
class Range {
  lo: number; hi: number;
  constructor(lo: number, hi: number) { this.lo = lo; this.hi = hi; }
  [Symbol.iterator](): Iterator<number> {
    let cur = this.lo;
    const hi = this.hi;
    return {
      next(): IteratorResult<number> {
        if (cur < hi) return { value: cur++, done: false };
        return { value: 0, done: true };
      },
    };
  }
}
for (const x of new Range(1, 4)) console.log(x);
console.log([...new Range(0, 3)]);
console.log(Array.from(new Range(5, 7)), Math.max(...new Range(2, 9)));
class Bag {
  items: string[] = ["a", "b"];
  *[Symbol.iterator](): Generator<string> { yield* this.items; }
}
console.log([...new Bag()], Array.from(new Bag()));
class Words {
  text = "x y z";
  [Symbol.iterator](): IterableIterator<string> {
    const parts = this.text.split(" ");
    let i = 0;
    return {
      next(): IteratorResult<string> { return i < parts.length ? { value: parts[i++], done: false } : { done: true, value: undefined }; },
      [Symbol.iterator]() { return this; },
    } as IterableIterator<string>;
  }
}
for (const w of new Words()) console.log(w);
function* nums(): Generator<number> { yield 1; yield 2; yield 3; }
function* words(s: string): Generator<string> { for (const w of s.split(" ")) yield w; }
const a = [...nums()];
console.log(a, [0, ...nums(), 9], [...words("a b c")]);
console.log(Array.from(nums()), Array.from(words("x y")));
console.log(Array.from(nums(), (x: number): number => x * 2));
console.log(Math.max(...nums()), Math.min(...nums()));
class Bag2 {
  items: string[] = ["a", "b"];
  *[Symbol.iterator](): Generator<string> { for (const i of this.items) yield i + "!"; }
}
const bag = new Bag2();
console.log([...bag], Array.from(bag), new Set(nums()).size);
// (last: Node runs the awaits after the program's synchronous code)
class Ticks {
  n = 3;
  [Symbol.asyncIterator](): AsyncIterator<number> {
    let i = 0;
    const n = this.n;
    return {
      async next(): Promise<IteratorResult<number>> {
        await null;
        if (i < n) return { value: i++, done: false };
        return { value: 0, done: true };
      },
    };
  }
}
async function main(): Promise<void> {
  for await (const t of new Ticks()) console.log("tick", t);
}
main();
