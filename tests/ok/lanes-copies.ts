// lane copies: N copies of the same double arithmetic become one vector in C (emit.c, ln_plan); every value must be
// what each copy computes alone, -0, NaN and Infinity included

// four copies, statement after statement of each copy, the escape of each read back one by one
function quad(a0: number, a1: number, a2: number, a3: number, c: number): number[] {
  let x0 = a0, x1 = a1, x2 = a2, x3 = a3;
  let y0 = c, y1 = c, y2 = c, y3 = c;
  for (let i = 0; i < 6; i++) {
    const s0 = x0 * x0, s1 = x1 * x1, s2 = x2 * x2, s3 = x3 * x3;
    y0 = 2 * x0 * y0 + c; x0 = s0 - y0 * y0 / 3 + a0;
    y1 = 2 * x1 * y1 + c; x1 = s1 - y1 * y1 / 3 + a1;
    y2 = 2 * x2 * y2 + c; x2 = s2 - y2 * y2 / 3 + a2;
    y3 = 2 * x3 * y3 + c; x3 = s3 - y3 * y3 / 3 + a3;
  }
  return [x0, x1, x2, x3, y0, y1, y2, y3, -x0 - y3];
}

// eight copies, each statement's copies together; bits packed with the first copy in the highest bit, and a test
// of all eight that leaves early
function bits(r0: number, r1: number, r2: number, r3: number, r4: number, r5: number, r6: number, r7: number, ci: number, early: boolean): int {
  let zr0 = r0, zr1 = r1, zr2 = r2, zr3 = r3, zr4 = r4, zr5 = r5, zr6 = r6, zr7 = r7;
  let zi0 = ci, zi1 = ci, zi2 = ci, zi3 = ci, zi4 = ci, zi5 = ci, zi6 = ci, zi7 = ci;
  for (let i = 0; i < 10; i++) {
    const t0 = zr0 * zr0 - zi0 * zi0 + r0, t1 = zr1 * zr1 - zi1 * zi1 + r1, t2 = zr2 * zr2 - zi2 * zi2 + r2, t3 = zr3 * zr3 - zi3 * zi3 + r3;
    const t4 = zr4 * zr4 - zi4 * zi4 + r4, t5 = zr5 * zr5 - zi5 * zi5 + r5, t6 = zr6 * zr6 - zi6 * zi6 + r6, t7 = zr7 * zr7 - zi7 * zi7 + r7;
    zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
    zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
    zr0 = t0; zr1 = t1; zr2 = t2; zr3 = t3; zr4 = t4; zr5 = t5; zr6 = t6; zr7 = t7;
    if (early) {
      const m0 = zr0 * zr0 + zi0 * zi0, m1 = zr1 * zr1 + zi1 * zi1, m2 = zr2 * zr2 + zi2 * zi2, m3 = zr3 * zr3 + zi3 * zi3;
      const m4 = zr4 * zr4 + zi4 * zi4, m5 = zr5 * zr5 + zi5 * zi5, m6 = zr6 * zr6 + zi6 * zi6, m7 = zr7 * zr7 + zi7 * zi7;
      if (!(m0 <= 4) && !(m1 <= 4) && !(m2 <= 4) && !(m3 <= 4) && !(m4 <= 4) && !(m5 <= 4) && !(m6 <= 4) && !(m7 <= 4)) return -1;
    }
  }
  let b = 0;
  if (zr0 * zr0 + zi0 * zi0 <= 4) b |= 128;
  if (zr1 * zr1 + zi1 * zi1 <= 4) b |= 64;
  if (zr2 * zr2 + zi2 * zi2 <= 4) b |= 32;
  if (zr3 * zr3 + zi3 * zi3 <= 4) b |= 16;
  if (zr4 * zr4 + zi4 * zi4 <= 4) b |= 8;
  if (zr5 * zr5 + zi5 * zi5 <= 4) b |= 4;
  if (zr6 * zr6 + zi6 * zi6 <= 4) b |= 2;
  if (zr7 * zr7 + zi7 * zi7 <= 4) b |= 1;
  return b;
}

// copies that are not independent (a copy reads another's variable, one is written alone): left as they are
function mixed(a: number, b: number, c: number, d: number): number[] {
  let p0 = a, p1 = b, p2 = c, p3 = d;
  p0 = p0 * 2 + p1; p1 = p1 * 2 + p2; p2 = p2 * 2 + p3; p3 = p3 * 2 + p0;
  let q0 = a, q1 = b, q2 = c, q3 = d;
  q0 = q0 * q0 - 1; q1 = q1 * q1 - 1; q2 = q2 * q2 - 1; q3 = q3 * q3 - 1;
  q2 = -q2;
  return [p0, p1, p2, p3, q0, q1, q2, q3];
}

console.log(quad(0.25, -0.5, 1e-300, -0, 0.125).join(" "));
console.log(quad(1e200, -1e200, NaN, Infinity, -0).join(" "));
console.log(1 / quad(-0, -0, -0, -0, -0)[0], 1 / quad(0, 0, 0, 0, -0)[4], 1 / quad(0, -0, 0, -0, 0)[8]);
const w = 64;
let rows = "";
for (let y = 0; y < 24; y++) {
  const ci = 2 * y / 24 - 1;
  let line = "";
  for (let x = 0; x < w; x += 8) {
    const r = (k: int): number => 2 * (x + k) / w - 1.5;
    line += bits(r(0), r(1), r(2), r(3), r(4), r(5), r(6), r(7), ci, false).toString(16).padStart(2, "0");
    line += bits(r(0), r(1), r(2), r(3), r(4), r(5), r(6), r(7), ci, true) < 0 ? "-" : "+";
  }
  rows += line + "\n";
}
console.log(rows);
console.log(mixed(1, 2, 3, 4).join(" "), mixed(-0, 0.5, NaN, 1e308).join(" "));
