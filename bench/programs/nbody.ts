// Ponto flutuante: simulação n-body (Sol + 4 planetas), versão do
// Computer Language Benchmarks Game adaptada para o subconjunto xisto-0.
const PI: number = 3.141592653589793;
const SOLAR_MASS: number = 4 * PI * PI;
const DAYS_PER_YEAR: number = 365.24;
const N: int = 5;

const x: number[] = new Array<number>(N).fill(0);
const y: number[] = new Array<number>(N).fill(0);
const z: number[] = new Array<number>(N).fill(0);
const vx: number[] = new Array<number>(N).fill(0);
const vy: number[] = new Array<number>(N).fill(0);
const vz: number[] = new Array<number>(N).fill(0);
const mass: number[] = new Array<number>(N).fill(0);

function setBody(i: int, bx: number, by: number, bz: number, bvx: number, bvy: number, bvz: number, m: number): void {
  x[i] = bx;
  y[i] = by;
  z[i] = bz;
  vx[i] = bvx * DAYS_PER_YEAR;
  vy[i] = bvy * DAYS_PER_YEAR;
  vz[i] = bvz * DAYS_PER_YEAR;
  mass[i] = m * SOLAR_MASS;
}

function init(): void {
  setBody(0, 0, 0, 0, 0, 0, 0, 1);
  setBody(1, 4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
    1.66007664274403694e-03, 7.69901118419740425e-03, -6.90460016972063023e-05, 9.54791938424326609e-04);
  setBody(2, 8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
    -2.76742510726862411e-03, 4.99852801234917238e-03, 2.30417297573763929e-05, 2.85885980666130812e-04);
  setBody(3, 1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
    2.96460137564761618e-03, 2.37847173959480950e-03, -2.96589568540237556e-05, 4.36624404335156298e-05);
  setBody(4, 1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
    2.68067772490389322e-03, 1.62824170038242295e-03, -9.51592254519715870e-05, 5.15138902046611451e-05);

  let px: number = 0;
  let py: number = 0;
  let pz: number = 0;
  for (let i: int = 0; i < N; i++) {
    px += vx[i] * mass[i];
    py += vy[i] * mass[i];
    pz += vz[i] * mass[i];
  }
  vx[0] = -px / SOLAR_MASS;
  vy[0] = -py / SOLAR_MASS;
  vz[0] = -pz / SOLAR_MASS;
}

function advance(dt: number): void {
  for (let i: int = 0; i < N; i++) {
    for (let j: int = i + 1; j < N; j++) {
      const dx: number = x[i] - x[j];
      const dy: number = y[i] - y[j];
      const dz: number = z[i] - z[j];
      const d2: number = dx * dx + dy * dy + dz * dz;
      const mag: number = dt / (d2 * Math.sqrt(d2));
      vx[i] -= dx * mass[j] * mag;
      vy[i] -= dy * mass[j] * mag;
      vz[i] -= dz * mass[j] * mag;
      vx[j] += dx * mass[i] * mag;
      vy[j] += dy * mass[i] * mag;
      vz[j] += dz * mass[i] * mag;
    }
  }
  for (let i: int = 0; i < N; i++) {
    x[i] += dt * vx[i];
    y[i] += dt * vy[i];
    z[i] += dt * vz[i];
  }
}

function energy(): number {
  let e: number = 0;
  for (let i: int = 0; i < N; i++) {
    e += 0.5 * mass[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
    for (let j: int = i + 1; j < N; j++) {
      const dx: number = x[i] - x[j];
      const dy: number = y[i] - y[j];
      const dz: number = z[i] - z[j];
      e -= (mass[i] * mass[j]) / Math.sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  return e;
}

const steps: int = 10000000;
init();
console.log(energy());
for (let s: int = 0; s < steps; s++) {
  advance(0.01);
}
console.log(energy());
