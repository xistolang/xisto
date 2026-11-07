// expect: callback of map must be (number) => int, but this is (int) => int
const xs = [1, 2].map((x: int): int => x + 1);
console.log(xs.length);
