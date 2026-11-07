/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

n-body, xisto #2: the program written for xisto rather than translated from JS. The bodies are columns of
numbers (x, y, z, vx, vy, vz, mass), and each step goes in three passes, as the fastest sequential Rust
program does: the ten pairs' differences, then their ten magnitudes (independent of each other, so the
processor overlaps the divisions and square roots), then the velocities, then the positions.
 */

const PI = Math.PI;
const SOLAR_MASS = 4 * PI * PI;
const DAYS_PER_YEAR = 365.24;
const N: int = 5;
const PAIRS: int = 10;

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
  let k: int = 0;
  for (let i = 0; i < N; i++) {
    for (let j = i + 1; j < N; j++) {
      dx[k] = x[i] - x[j];
      dy[k] = y[i] - y[j];
      dz[k] = z[i] - z[j];
      k++;
    }
  }
  for (let p = 0; p < PAIRS; p++) {
    const d2 = dx[p] * dx[p] + dy[p] * dy[p] + dz[p] * dz[p];
    mag[p] = dt / (d2 * Math.sqrt(d2));
  }
  k = 0;
  for (let i = 0; i < N; i++) {
    for (let j = i + 1; j < N; j++) {
      const mj = mass[j] * mag[k], mi = mass[i] * mag[k];
      vx[i] -= dx[k] * mj;
      vy[i] -= dy[k] * mj;
      vz[i] -= dz[k] * mj;
      vx[j] += dx[k] * mi;
      vy[j] += dy[k] * mi;
      vz[j] += dz[k] * mi;
      k++;
    }
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
