// expect: runtime error: integer overflow: the result left JavaScript's safe integer range
let big: int = 4503599627370496; // 2^52
big = big * 2; // 2^53: Node would go on with rounded numbers; an int stops here
console.log(big);
