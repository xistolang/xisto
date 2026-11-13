// expect: runtime error: index out of bounds
// a counted loop with <= (emit.c, mark_counted_inc) one past the row: the guard fails and the checked copy reports
// the index, uncaught
const row = [1, 2, 3, 4];
let s = 0;
for (let j = 0; j <= row.length - 0 + 4 - 4; j++) s += row[j];
console.log(s);
