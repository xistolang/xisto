// Setting an array's length to cut it (and to 0) works as in Node; the same length is no change
const xs = [1, 2, 3, 4, 5];
xs.length = 3;
console.log(xs, xs.length);
xs.length = 3;
console.log(xs);
xs.length = 0;
console.log(xs, xs.length);
const ys = new Array(4).fill("a");
ys.length = 2;
ys.push("b");
console.log(ys);
function cut(zs: string[], n: int): string[] {
  zs.length = n;
  return zs;
}
console.log(cut(["p", "q", "r"], 1), cut([], 0));
