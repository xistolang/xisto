// Closures that capture by reference: a captured variable that is reassigned is shared (heap cell); loops keep
// JavaScript's copy per iteration; captured objects, counters, accumulators, nested closures and parameters.
function makeCounter(start: int): () => int {
  let n = start;
  return (): int => {
    n++;
    return n;
  };
}
const c1 = makeCounter(10);
const c2 = makeCounter(0);
console.log(c1(), c1(), c2(), c1());

let total: int = 0;
const xs: int[] = [1, 2, 3, 4];
xs.forEach((x: int): void => {
  total += x;
});
console.log("total", total);

function sumWith(list: int[]): int {
  let acc: int = 0;
  let calls: int = 0;
  list.forEach((x: int): void => {
    acc += x * 2;
    calls++;
  });
  return acc * 100 + calls;
}
console.log(sumWith([1, 2, 3]));

const fns: Array<() => int> = [];
for (let i: int = 0; i < 3; i++) {
  fns.push((): int => i);
}
console.log(fns.map((f: () => int): int => f()));

function lastName(names: string[]): string {
  let last = "none";
  names.forEach((s: string): void => {
    last = s;
  });
  return last;
}
console.log(lastName(["ana", "bia"]), lastName([]));

function outer(): int {
  let depth: int = 0;
  const inc = (): void => {
    const deeper = (): void => {
      depth += 10;
    };
    deeper();
    depth++;
  };
  inc();
  inc();
  return depth;
}
console.log(outer());

function param(p: string): string {
  const f = (): void => {
    p = p + "!";
  };
  f();
  f();
  return p;
}
console.log(param("hey"));

let shared: string[] = [];
const add = (s: string): void => {
  shared.push(s);
};
add("a");
shared = ["fresh"];
add("b");
console.log(shared);
