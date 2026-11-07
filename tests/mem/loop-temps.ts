// Temporaries made by loop conditions and if conditions must be released every iteration
// (review: 3M iterations peaked at 202 MB). tests/run.sh checks the peak RSS on both backends.
function tag(i: int): string {
  return "k";
}
function work(): int {
  let n: int = 0;
  for (let i: int = 0; i < 3000000; i++) {
    if (tag(i) + "x" === "kx") {
      n++;
    }
  }
  let j: int = 0;
  while (tag(j) + "y" === "ky" && j < 3000000) {
    j++;
  }
  for (let k: int = 0; (tag(k) + "z").length === 2 && k < 3000000; k = k + 1) {
    n++;
  }
  return n + j;
}
console.log(work());
