// lanes, mask-only (emit.c lane_loop): the rest of the body reads the inner loop's state only through the loop's
// own condition, so no lane keeps its values from the iteration it stopped: the condition there is (the lane ran
// every iteration) && condition. The condition here is not monotonic (the logistic map goes above 0.8 and back
// below), so a lane that would keep iterating could come back: the answer must still be the stopped one's.

function logistic(from: int, to: int, k: number): string {
  let out = "";
  let inside = 0;
  for (let x = from; x < to; x++) {
    let v = 0.0;
    const r = 2.6 + x / 40;
    for (let i = 0; i < 37 && v <= 0.8; i++) {
      v = r * (v + 0.05) * (1 - v) * k;
    }
    if (v <= 0.8) inside++;
    out += v <= 0.8 ? "#" : ".";
  }
  return `${from}..${to}: ${inside} ${out}`;
}
console.log(logistic(0, 29, 1));
console.log(logistic(-5, 64, 0.97));
console.log(logistic(3, 3, 1));
console.log(logistic(0, 8, 0.5));

// a strict condition and a bound of 0 iterations
function strict(n: int, K: int): string {
  let s = "";
  for (let x = 0; x < n; x++) {
    let a = x / 10, b = 0.0;
    const c = 0.3 - x / 40;
    for (let i = 0; i < K && a * a + b * b < 4; i++) {
      const t = a * a - b * b + c;
      b = 2 * a * b + c;
      a = t;
    }
    s += a * a + b * b < 4 ? "1" : "0";
  }
  return s;
}
console.log(strict(19, 0), strict(19, 5), strict(33, 50));
