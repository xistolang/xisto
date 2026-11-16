// (tests/size/lean-pkg.ts) a package's setup function with a loop, called once by the program
export function setup(n: int): int {
  let s: int = 0;
  for (let i: int = 0; i < n; i++) s += i;
  return s;
}
