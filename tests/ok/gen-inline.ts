// Generators the compiler inlines into their for...of: break, continue, return from the loop body, return
// inside the generator, nested generators (evens(naturals(n))), two yields, strings and objects (refcounts),
// iterable classes, shadowed names, and a generator whose stream is iterated conditionally.
function* range(from: int, to: int): Generator<int> {
  for (let i: int = from; i < to; i++) {
    yield i;
  }
}
function* evens(src: Generator<int>): Generator<int> {
  for (const x of src) {
    if (x % 2 === 0) {
      yield x;
    }
  }
}
function* words(text: string): Generator<string> {
  for (const w of text.split(" ")) {
    if (w.length === 0) {
      continue;
    }
    if (w === "stop") {
      return;
    }
    yield w.toUpperCase();
  }
}
function* pairs(n: int): Generator<string> {
  for (let i: int = 0; i < n; i++) {
    const tag = "p" + String(i);
    yield tag + "a";
    yield tag + "b";
  }
}
function* maybe(src: Generator<int>, use: boolean): Generator<int> {
  yield -1;
  if (use) {
    for (const v of src) {
      yield v * 10;
    }
  }
  yield -2;
}
class Box {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
}
function* boxes(n: int): Generator<Box> {
  for (let i: int = 0; i < n; i++) {
    yield new Box("box" + String(i));
  }
}
class Matrix {
  rows: int;
  cols: int;
  constructor(rows: int, cols: int) {
    this.rows = rows;
    this.cols = cols;
  }
  *[Symbol.iterator](): Iterator<string> {
    for (let r: int = 0; r < this.rows; r++) {
      for (let c: int = 0; c < this.cols; c++) {
        yield String(r) + ":" + String(c);
      }
    }
  }
}

function firstOver(limit: int): int {
  for (const x of range(0, 1000)) {
    if (x * x > limit) {
      return x;
    }
  }
  return -1;
}

function findWord(target: string): string {
  const prefix = "found ";
  for (const w of words("alpha  beta gamma delta")) {
    if (w === target) {
      return prefix + w;
    }
  }
  return "none";
}

let total: int = 0;
for (const x of evens(range(0, 20))) {
  if (x === 4) {
    continue;
  }
  if (x > 14) {
    break;
  }
  total += x;
}
console.log("evens", total);

// a caller variable with the same name as the generator's locals
const i: int = 1000;
let acc: int = 0;
for (const x of range(0, 3)) {
  acc += x + i;
}
console.log("shadow", acc);

const ws: string[] = [];
for (const w of words("one  two stop three")) {
  ws.push(w);
}
console.log("words", ws.join(","));

const ps: string[] = [];
for (const p of pairs(3)) {
  if (p === "p1b") {
    continue;
  }
  ps.push(p);
  if (p === "p2a") {
    break;
  }
}
console.log("pairs", ps.join(","));

const ms: int[] = [];
for (const v of maybe(range(1, 4), true)) {
  ms.push(v);
}
for (const v of maybe(range(1, 4), false)) {
  ms.push(v);
}
console.log("maybe", ms.join(","));

let names = "";
for (const b of boxes(4)) {
  names += b.name + ";";
  if (b.name === "box2") {
    break;
  }
}
console.log("boxes", names);

const cells: string[] = [];
for (const c of new Matrix(2, 3)) {
  cells.push(c);
}
console.log("matrix", cells.join(" "));

console.log("firstOver", firstOver(50), firstOver(10000000));
console.log("findWord", findWord("GAMMA"), findWord("zeta"));

// nested loops: the inner generator restarts for each outer value
let grid = "";
for (const a of range(0, 3)) {
  for (const b of range(0, a + 1)) {
    grid += `${a}${b} `;
  }
}
console.log("grid", grid);

// a generator kept as a value still works (not inlined)
const g = range(5, 8);
const kept: int[] = [];
for (const x of g) {
  kept.push(x);
}
console.log("kept", kept.join(","));
