// expect: runtime error: integer overflow
let big: int = 4611686018427387904;
big = big * 2;
console.log(big);
