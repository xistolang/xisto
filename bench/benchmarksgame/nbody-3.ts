/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

n-body, xisto #3: #2 (the bodies as columns of numbers, each step in three passes) with every pass a loop over
the ten pairs indexed only by its own variable p: the two bodies of pair p come from the constant tables PI and
PJ, so no counter has to be carried across the nested loops. Same operations in the same order as #2 (and as
the translated program), so the same result to the last bit.
 */
const PI = Math.PI;
const SOLAR_MASS = 4 * PI * PI;
const DAYS_PER_YEAR = 365.24;
const N: int = 5;
const PAIRS: int = 10;
// pair p is (PI[p], PJ[p]): the (i, j) with i < j in the order of the nested loops
const PI_: int[] = [0, 0, 0, 0, 1, 1, 1, 2, 2, 3];
const PJ_: int[] = [1, 2, 3, 4, 2, 3, 4, 3, 4, 4];

const x: number[] = [0, 4.84143144246472090e+00, 8.34336671824457987e+00, 1.28943695621391310e+01, 1.53796971148509165e+01];
const y: number[] = [0, -1.16032004402742839e+00, 4.12479856412430479e+00, -1.51111514016986312e+01, -2.59193146099879641e+01];
const z: number[] = [0, -1.03622044471123109e-01, -4.03523417114321381e-01, -2.23307578892655734e-01, 1.79258772950371181e-01];
const vx: number[] = [0, 1.66007664274403694e-03 * DAYS_PER_YEAR, -2.76742510726862411e-03 * DAYS_PER_YEAR,
  2.96460137564761618e-03 * DAYS_PER_YEAR, 2.68067772490389322e-03 * DAYS_PER_YEAR];
const vy: number[] = [0, 7.69901118419740425e-03 * DAYS_PER_YEAR, 4.99852801234917238e-03 * DAYS_PER_YEAR,
  2.37847173959480950e-03 * DAYS_PER_YEAR, 1.62824170038242295e-03 * DAYS_PER_YEAR];
const vz: number[] = [0, -6.90460016972063023e-05 * DAYS_PER_YEAR, 2.30417297573763929e-05 * DAYS_PER_YEAR,
  -2.96589568540237556e-05 * DAYS_PER_YEAR, -9.51592254519715870e-05 * DAYS_PER_YEAR];
const mass: number[] = [SOLAR_MASS, 9.54791938424326609e-04 * SOLAR_MASS, 2.85885980666130812e-04 * SOLAR_MASS,
  4.36624404335156298e-05 * SOLAR_MASS, 5.15138902046611451e-05 * SOLAR_MASS];

const dx: number[] = new Array<number>(PAIRS).fill(0);
const dy: number[] = new Array<number>(PAIRS).fill(0);
const dz: number[] = new Array<number>(PAIRS).fill(0);
const mag: number[] = new Array<number>(PAIRS).fill(0);

function offsetMomentum(): void {
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

function energy(): number {
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

function advance(dt: number): void {
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

const n: int = Math.trunc(Number(process.argv[2]));
offsetMomentum();
console.log(energy().toFixed(9));
for (let i = 0; i < n; i++) advance(0.01);
console.log(energy().toFixed(9));
