/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

n-body, xisto #4: #3 (columns, three passes over the ten pairs indexed by p) with the state in Float64Arrays
that are locals of the function running the steps, handed to advance/energy as parameters. A small Float64Array
that a function only indexes lives in its frame (no heap object), so the C compiler keeps the five positions,
velocities, masses, ten differences and ten magnitudes in registers across the unrolled loops, as the Rust
program's [f64; N] arrays do. Same operations in the same order as #3: the same result to the last bit.
 */
const PI = Math.PI;
const SOLAR_MASS = 4 * PI * PI;
const DAYS_PER_YEAR = 365.24;
const N: int = 5;
const PAIRS: int = 10;
// pair p is (PI[p], PJ[p]): the (i, j) with i < j in the order of the nested loops
const PI_: int[] = [0, 0, 0, 0, 1, 1, 1, 2, 2, 3];
const PJ_: int[] = [1, 2, 3, 4, 2, 3, 4, 3, 4, 4];

function offsetMomentum(vx: Float64Array, vy: Float64Array, vz: Float64Array, mass: Float64Array): void {
  let px = 0, py = 0, pz = 0;
  for (let i = 0; i < N; i++) {
    px += vx[i] * mass[i];
    py += vy[i] * mass[i];
    pz += vz[i] * mass[i];
  }
  vx[0] = -px / SOLAR_MASS;
  vy[0] = -py / SOLAR_MASS;
  vz[0] = -pz / SOLAR_MASS;
}

function energy(x: Float64Array, y: Float64Array, z: Float64Array, vx: Float64Array, vy: Float64Array, vz: Float64Array, mass: Float64Array): number {
  let e = 0;
  for (let i = 0; i < N; i++) {
    e += 0.5 * mass[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
    for (let j = i + 1; j < N; j++) {
      const ex = x[i] - x[j], ey = y[i] - y[j], ez = z[i] - z[j];
      e -= (mass[i] * mass[j]) / Math.sqrt(ex * ex + ey * ey + ez * ez);
    }
  }
  return e;
}

function advance(dt: number, x: Float64Array, y: Float64Array, z: Float64Array, vx: Float64Array, vy: Float64Array, vz: Float64Array,
  mass: Float64Array, dx: Float64Array, dy: Float64Array, dz: Float64Array, mag: Float64Array): void {
  for (let p = 0; p < PAIRS; p++) {
    const i = PI_[p], j = PJ_[p];
    dx[p] = x[i] - x[j];
    dy[p] = y[i] - y[j];
    dz[p] = z[i] - z[j];
  }
  for (let p = 0; p < PAIRS; p++) {
    const d2 = dx[p] * dx[p] + dy[p] * dy[p] + dz[p] * dz[p];
    mag[p] = dt / (d2 * Math.sqrt(d2));
  }
  for (let p = 0; p < PAIRS; p++) {
    const i = PI_[p], j = PJ_[p];
    const mj = mass[j] * mag[p], mi = mass[i] * mag[p];
    vx[i] -= dx[p] * mj;
    vy[i] -= dy[p] * mj;
    vz[i] -= dz[p] * mj;
    vx[j] += dx[p] * mi;
    vy[j] += dy[p] * mi;
    vz[j] += dz[p] * mi;
  }
  for (let i = 0; i < N; i++) {
    x[i] += vx[i] * dt;
    y[i] += vy[i] * dt;
    z[i] += vz[i] * dt;
  }
}

function main(n: int): void {
  const x = new Float64Array(N), y = new Float64Array(N), z = new Float64Array(N);
  const vx = new Float64Array(N), vy = new Float64Array(N), vz = new Float64Array(N);
  const mass = new Float64Array(N);
  const dx = new Float64Array(PAIRS), dy = new Float64Array(PAIRS), dz = new Float64Array(PAIRS), mag = new Float64Array(PAIRS);
  // sun, jupiter, saturn, uranus, neptune
  x[1] = 4.84143144246472090e+00; y[1] = -1.16032004402742839e+00; z[1] = -1.03622044471123109e-01;
  vx[1] = 1.66007664274403694e-03 * DAYS_PER_YEAR; vy[1] = 7.69901118419740425e-03 * DAYS_PER_YEAR; vz[1] = -6.90460016972063023e-05 * DAYS_PER_YEAR;
  x[2] = 8.34336671824457987e+00; y[2] = 4.12479856412430479e+00; z[2] = -4.03523417114321381e-01;
  vx[2] = -2.76742510726862411e-03 * DAYS_PER_YEAR; vy[2] = 4.99852801234917238e-03 * DAYS_PER_YEAR; vz[2] = 2.30417297573763929e-05 * DAYS_PER_YEAR;
  x[3] = 1.28943695621391310e+01; y[3] = -1.51111514016986312e+01; z[3] = -2.23307578892655734e-01;
  vx[3] = 2.96460137564761618e-03 * DAYS_PER_YEAR; vy[3] = 2.37847173959480950e-03 * DAYS_PER_YEAR; vz[3] = -2.96589568540237556e-05 * DAYS_PER_YEAR;
  x[4] = 1.53796971148509165e+01; y[4] = -2.59193146099879641e+01; z[4] = 1.79258772950371181e-01;
  vx[4] = 2.68067772490389322e-03 * DAYS_PER_YEAR; vy[4] = 1.62824170038242295e-03 * DAYS_PER_YEAR; vz[4] = -9.51592254519715870e-05 * DAYS_PER_YEAR;
  mass[0] = SOLAR_MASS;
  mass[1] = 9.54791938424326609e-04 * SOLAR_MASS;
  mass[2] = 2.85885980666130812e-04 * SOLAR_MASS;
  mass[3] = 4.36624404335156298e-05 * SOLAR_MASS;
  mass[4] = 5.15138902046611451e-05 * SOLAR_MASS;
  offsetMomentum(vx, vy, vz, mass);
  console.log(energy(x, y, z, vx, vy, vz, mass).toFixed(9));
  for (let i = 0; i < n; i++) advance(0.01, x, y, z, vx, vy, vz, mass, dx, dy, dz, mag);
  console.log(energy(x, y, z, vx, vy, vz, mass).toFixed(9));
}

main(Math.trunc(Number(process.argv[2])));
