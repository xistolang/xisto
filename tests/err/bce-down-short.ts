// expect: runtime error: index out of bounds
// a loop that counts down from past the row (emit.c, bce_version_guard): the guard fails and the copy with the
// checks reports the first index, uncaught
const row = new Float32Array(4);
let s = 0;
for (let j = 4; j >= 0; j--) s += row[j];
console.log(s);
