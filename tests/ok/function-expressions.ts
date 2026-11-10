const mk = function (a: number, b: number): number { return a + b; };
console.log(mk(1, 2));
function deco(n: string): (o: object, p: string) => void {
  return function (o: object, p: string): void {
    console.log(n, p);
  };
}
deco("x")({}, "campo");
const named = function soma(...xs: number[]): number { let t = 0; for (const x of xs) t += x; return t; };
console.log(named(1, 2, 3));
