// Generators: 3M values through a filter of a generator over a generator, plus an iterable class.
function* naturals(limit: int): Generator<int> {
  for (let i: int = 0; i < limit; i++) {
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
class Grid {
  w: int;
  h: int;
  constructor(w: int, h: int) {
    this.w = w;
    this.h = h;
  }
  *[Symbol.iterator](): Iterator<int> {
    for (let y: int = 0; y < this.h; y++) {
      for (let x: int = 0; x < this.w; x++) {
        yield y * this.w + x;
      }
    }
  }
}
let sum: int = 0;
for (const x of evens(naturals(3000000))) {
  sum += x;
}
let cells: int = 0;
for (const c of new Grid(1000, 1000)) {
  cells += c % 3;
}
console.log(sum, cells);
