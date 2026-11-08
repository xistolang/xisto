// const of an integer literal, and ?: between integer literals, are ints where an int is needed (and stay
// numbers where a number is: x / 2 is 2.5)
function f(n: int): void {
  const a = 5;
  const ya: int = a;
  const x = n > 2 ? 1 : 8;
  const y: int = x;
  let z = n > 2 ? -1 : 8;
  z += 1;
  const w: int = z;
  console.log(ya, y, w, a / 2, x / 2);
}
f(3);
f(1);
const per = 4 > 2 ? 1 : 8;
const arr = new Array<int>(per).fill(0);
console.log(arr.length, per * 2.5);
