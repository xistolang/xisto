function f(n: int): int {
  if (n < 0) throw new TypeError("negative: " + n);
  if (n > 9) throw new RangeError("too big");
  if (n === 5) throw new SyntaxError("five");
  return n;
}
const vals: int[] = [1, -1, 10, 5];
for (const v of vals) {
  try {
    console.log(f(v));
  } catch (e) {
    if (e instanceof TypeError) console.log("type", e.name, e.message);
    else if (e instanceof RangeError) console.log("range", e.name, e.message);
    else if (e instanceof Error) console.log("other", e.name, e.message);
    if (e instanceof Error) console.log("is Error", String(e));
  }
}
