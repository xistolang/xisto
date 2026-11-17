// Array literals that mix values and arrays (nested to different depths), and flat() over them, as Node gives them
const a = [[1], [2, [3]]];
console.log(a, a.flat(), a.flat(2), a.flat(Infinity), a.flat(0));
const b = [1, [2, 3], 4];
console.log(b.flat(), b.length, b[1]);
const c = ["x", ["y", "z"]].flat();
console.log(c, c.length, c.join("-"));
const d = [[[1, 2]], [[3], 4]];
console.log(d.flat(), d.flat(2), d.flat(3));
console.log([[1], [2, [3]]].flat(2).map((x) => x * 10));
