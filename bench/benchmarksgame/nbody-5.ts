/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

n-body, xisto #5: #4 (the state in Float64Array locals of the function running the steps, which live in its
frame) with each body's position and velocity side by side, x y z and a fourth lane that stays 0: body i is
p[4i .. 4i+3]. The four-lane loops over a body are what the C compiler turns into one vector instruction each
(a difference, the two velocity updates, the position step), as the Rust program #6 does with its F64x4.
Same operations in the same order as #4 for x, y and z (the fourth lane only ever holds 0): the same result to
the last bit.
 */
const PI = Math.PI;
const SOLAR_MASS = 4 * PI * PI;
const DAYS_PER_YEAR = 365.24;
const N: int = 5;
const PAIRS: int = 10;
const NS: int = 20; // N bodies × 4 lanes
const PS: int = 40; // PAIRS × 4 lanes
// pair q is (PI[q], PJ[q]): the (i, j) with i < j in the order of the nested loops
const PI_: int[] = [0, 0, 0, 0, 1, 1, 1, 2, 2, 3];
const PJ_: int[] = [1, 2, 3, 4, 2, 3, 4, 3, 4, 4];

function offsetMomentum(v: Float64Array, mass: Float64Array): void {
  let px = 0, py = 0, pz = 0;
  for (let i = 0; i < N; i++) {
    px += v[4 * i] * mass[i];
    py += v[4 * i + 1] * mass[i];
    pz += v[4 * i + 2] * mass[i];
  }
  v[0] = -px / SOLAR_MASS;
  v[1] = -py / SOLAR_MASS;
  v[2] = -pz / SOLAR_MASS;
}

function energy(p: Float64Array, v: Float64Array, mass: Float64Array): number {
  let e = 0;
  for (let i = 0; i < N; i++) {
    e += 0.5 * mass[i] * (v[4 * i] * v[4 * i] + v[4 * i + 1] * v[4 * i + 1] + v[4 * i + 2] * v[4 * i + 2]);
    for (let j = i + 1; j < N; j++) {
      const ex = p[4 * i] - p[4 * j], ey = p[4 * i + 1] - p[4 * j + 1], ez = p[4 * i + 2] - p[4 * j + 2];
      e -= (mass[i] * mass[j]) / Math.sqrt(ex * ex + ey * ey + ez * ez);
    }
  }
  return e;
}

function advance(dt: number, p: Float64Array, v: Float64Array, mass: Float64Array, d: Float64Array, mag: Float64Array): void {
  for (let q = 0; q < PAIRS; q++) {
    const i = PI_[q], j = PJ_[q];
    for (let k = 0; k < 4; k++) d[4 * q + k] = p[4 * i + k] - p[4 * j + k];
  }
  for (let q = 0; q < PAIRS; q++) {
    const d2 = d[4 * q] * d[4 * q] + d[4 * q + 1] * d[4 * q + 1] + d[4 * q + 2] * d[4 * q + 2];
    mag[q] = dt / (d2 * Math.sqrt(d2));
  }
  for (let q = 0; q < PAIRS; q++) {
    const i = PI_[q], j = PJ_[q];
    const mj = mass[j] * mag[q], mi = mass[i] * mag[q];
    for (let k = 0; k < 4; k++) {
      v[4 * i + k] -= d[4 * q + k] * mj;
      v[4 * j + k] += d[4 * q + k] * mi;
    }
  }
  for (let i = 0; i < NS; i++) p[i] += v[i] * dt;
}

function main(n: int): void {
  const p = new Float64Array(NS), v = new Float64Array(NS), mass = new Float64Array(N);
  const d = new Float64Array(PS), mag = new Float64Array(PAIRS);
  // sun, jupiter, saturn, uranus, neptune
  p[4] = 4.84143144246472090e+00; p[5] = -1.16032004402742839e+00; p[6] = -1.03622044471123109e-01;
  v[4] = 1.66007664274403694e-03 * DAYS_PER_YEAR; v[5] = 7.69901118419740425e-03 * DAYS_PER_YEAR; v[6] = -6.90460016972063023e-05 * DAYS_PER_YEAR;
  p[8] = 8.34336671824457987e+00; p[9] = 4.12479856412430479e+00; p[10] = -4.03523417114321381e-01;
  v[8] = -2.76742510726862411e-03 * DAYS_PER_YEAR; v[9] = 4.99852801234917238e-03 * DAYS_PER_YEAR; v[10] = 2.30417297573763929e-05 * DAYS_PER_YEAR;
  p[12] = 1.28943695621391310e+01; p[13] = -1.51111514016986312e+01; p[14] = -2.23307578892655734e-01;
  v[12] = 2.96460137564761618e-03 * DAYS_PER_YEAR; v[13] = 2.37847173959480950e-03 * DAYS_PER_YEAR; v[14] = -2.96589568540237556e-05 * DAYS_PER_YEAR;
  p[16] = 1.53796971148509165e+01; p[17] = -2.59193146099879641e+01; p[18] = 1.79258772950371181e-01;
  v[16] = 2.68067772490389322e-03 * DAYS_PER_YEAR; v[17] = 1.62824170038242295e-03 * DAYS_PER_YEAR; v[18] = -9.51592254519715870e-05 * DAYS_PER_YEAR;
  mass[0] = SOLAR_MASS;
  mass[1] = 9.54791938424326609e-04 * SOLAR_MASS;
  mass[2] = 2.85885980666130812e-04 * SOLAR_MASS;
  mass[3] = 4.36624404335156298e-05 * SOLAR_MASS;
  mass[4] = 5.15138902046611451e-05 * SOLAR_MASS;
  offsetMomentum(v, mass);
  console.log(energy(p, v, mass).toFixed(9));
  for (let i = 0; i < n; i++) advance(0.01, p, v, mass, d, mag);
  console.log(energy(p, v, mass).toFixed(9));
}

main(Math.trunc(Number(process.argv[2])));
