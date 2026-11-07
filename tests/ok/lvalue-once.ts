// Compound assignment and ++ evaluate the target once (review: a[i++] += 5 bumped i twice on QBE,
// s[j++] += "!" on both backends), Array(n).fill with references, Math.random on QBE.
const a: int[] = [10, 20, 30];
let i: int = 0;
a[i++] += 5;
console.log(a, i);
const s: string[] = ["x", "y", "z"];
let j: int = 0;
s[j++] += "!";
console.log(s, j);
const c: int[] = [1, 2, 3];
let k: int = 0;
c[k++]++;
console.log(c, k);
class Box {
  name: string = "b";
  n: int = 0;
}
let calls: int = 0;
const box: Box = new Box();
function getBox(): Box {
  calls++;
  return box;
}
getBox().name += "+";
getBox().n += 2;
getBox().n++;
console.log(box, calls);
const g: string[] = ["a"];
function grow(): string {
  for (let q: int = 0; q < 100; q++) {
    g.push("z");
  }
  return "!";
}
g[0] += grow();
console.log(g[0], g.length);
function mk(n: int): string {
  return `n${n}`;
}
const f: string[] = new Array<string>(3).fill(mk(1));
console.log(f);
let inRange: int = 0;
for (let r: int = 0; r < 1000; r++) {
  const x: number = Math.random();
  if (x >= 0 && x < 1) {
    inRange++;
  }
}
console.log(inRange);
