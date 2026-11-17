// A package's function that runs once is compiled for size (XS_RUN_ONCE), loop and all; the program's own kernel,
// also called once with a loop, keeps -O3 (tests/run.sh "lean", with XISTO_PKGS=tests/size/pkgs).
import { setup } from "leanpkg";
function kernel(n: int): int {
  let s: int = 0;
  for (let i: int = 0; i < n; i++) s += i * i;
  return s;
}
console.log(setup(10), kernel(10));
