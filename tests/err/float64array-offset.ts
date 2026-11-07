// expect: runtime error: RangeError: start offset of Float64Array should be a multiple of 8
const b = new SharedArrayBuffer(64);
const f = new Float64Array(b, 4, 2);
console.log(f.length);
