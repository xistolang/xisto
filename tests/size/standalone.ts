function f7(x: int): int {
  let s: int = x;
  for (let k: int = 0; k < 1; k++) {
    s = s * 3 + k;
  }
  return s + 7;
}

console.log(f7(3));
