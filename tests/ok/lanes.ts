// Loops whose body starts with a pure computation from the counter (numbers and an inner loop over them) run
// eight counters at a time in vector lanes (emit.c lane_loop): the same doubles and the same order of everything
// else as the loop written.

// escape-time, with the rest of the body reading the state and the counter
function escape(from: int, to: int, c: number): string {
  let out = "";
  let ones = 0;
  for (let x = from; x < to; x++) {
    let a = 0.0, b = 0.0;
    const r = x / 7 - 1.25;
    for (let i = 0; i < 40 && a * a + b * b <= 4; i++) {
      const t = a * a - b * b + r;
      b = 2 * a * b + c;
      a = t;
    }
    if (a * a + b * b <= 4) ones++;
    out += a * a + b * b <= 4 ? "#" : ".";
    if (x % 9 === 4) continue;
    switch (x % 3) {
      case 0:
        out += "";
        break;
      default:
        break;
    }
  }
  return `${from}..${to}: ${ones} ${out}`;
}
console.log(escape(0, 23, 0.1));
console.log(escape(-11, 30, -0.4));
console.log(escape(3, 5, 0.3));
console.log(escape(0, 8, 0.0));
console.log(escape(5, 2, 0.0));

// a condition that turns true again after it was false: each lane stops where its own loop stops
function bounce(n: int): string {
  const parts: string[] = [];
  for (let x = 0; x < n; ++x) {
    let v = x * 1.5;
    let steps = 0.0;
    for (let i = 0; i < 30 && v < 10; i++) {
      v = 13 - v * 0.5 + x * 0.25;
      steps = steps + 1;
    }
    parts.push(`${steps}:${v}`);
  }
  return parts.join(" ");
}
console.log(bounce(19));

// integer arithmetic past the bound the lanes check: the loop as written does it
function wide(n: int, big: int): number {
  let sum = 0.0;
  for (let x = 0; x < n; x++) {
    let v = x * big / 3;
    for (let i = 0; i < 3 && v > 1; i++) v = v / 2;
    sum += v;
  }
  return sum;
}
console.log(wide(10, 562949953421312));
console.log(wide(10, 1000));

// the rest of the body writes what the computation reads: no lanes, the same result
function shifting(n: int): number {
  let k = 1.0;
  let total = 0.0;
  for (let x = 0; x < n; x++) {
    let v = x + k;
    for (let i = 0; i < 4 && v < 50; i++) v = v * 2;
    total += v;
    k = k + 0.5;
  }
  return total;
}
console.log(shifting(20));
