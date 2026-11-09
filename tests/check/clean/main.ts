// a project that already compiles with xisto: check reports no changes and exits 0
import { readFileSync } from "xisto:fs";

function total(xs: int[]): int {
  let t: int = 0;
  for (const x of xs) t += x;
  return t;
}

console.log(total([1, 2, 3]), typeof readFileSync);
