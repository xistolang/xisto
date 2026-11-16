// new Array(n).fill(x) without <T>: the element type is the fill value's; Array(n) is new Array(n)
const a = new Array<number>(3).fill(7);
console.log(a);
const b = new Array(4).fill(0);
b[1] = 1.5;
console.log(b, b.length);
const s = new Array(2).fill("s");
console.log(s.join("-"));
const d: string[] = Array(2).fill("x");
console.log(d);
const e = Array<boolean>(3).fill(false);
e[2] = true;
console.log(e);
const grid = Array.from({ length: 3 }, (_, i) => new Array(i + 1).fill(i));
console.log(grid);
const g = Array.from({ length: 3 }, () => [] as number[]);
g[0].push(1);
console.log(g);
