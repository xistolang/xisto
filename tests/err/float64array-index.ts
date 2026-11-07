// expect: Float64Array index must be int, but this is number
const f = new Float64Array(4);
const x = 1.5;
console.log(f[x]);
