// console.log of function values: [Function: name] as JavaScript names them, [Function (anonymous)], [AsyncFunction: f]
const anon: string | (() => number) = () => 1;
console.log(anon);
const plain = () => 2;
console.log(plain);
function outer(): void {
  const inner = (x: number) => x;
  console.log(inner);
  function nested(): number { return 1; }
  console.log(nested);
  const u: number | (() => number) = () => 3;
  console.log(u);
  const fe = function named2() { return 1; };
  console.log(fe);
  const af = async () => 1;
  console.log(af);
}
outer();
async function asy(): Promise<number> { return 1; }
console.log(asy);
class K {
  h = (n: number) => n;
  m(): number { return 2; }
}
const k = new K();
console.log(k.h);
console.log([plain, () => 1]);
const o = { next: (v: number) => v, a: 1 };
console.log(o);
