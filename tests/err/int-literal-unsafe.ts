// expect: must be int, but this is number
// 2^53 + 1 is not a safe integer: the literal is a number (rounded, as in JS), not an int
let x: int = 9007199254740993;
console.log(x);
