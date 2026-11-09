// pad4 (x86 with AVX2, or XISTO_PAD4=1): fields Px Py Pz of a class held as one column of 4 lanes, and a line written
// once per coordinate copied for the fourth lane. The results must be the program's, to the last bit.
class P {
  x: number; y: number; z: number; vx: number; vy: number; vz: number; m: number;
  constructor(x: number, y: number, z: number, vx: number, vy: number, vz: number, m: number) {
    this.x = x; this.y = y; this.z = z; this.vx = vx; this.vy = vy; this.vz = vz; this.m = m;
  }
}

function step(ps: P[], dt: number): void {
  const n = ps.length;
  for (let i = 0; i < n; i++) {
    for (let j = i + 1; j < n; j++) {
      const dx = ps[i].x - ps[j].x;
      const dy = ps[i].y - ps[j].y;
      const dz = ps[i].z - ps[j].z;
      const d2 = dx * dx + dy * dy + dz * dz;
      const f = dt / (d2 * Math.sqrt(d2));
      ps[i].vx -= dx * ps[j].m * f;
      ps[i].vy -= dy * ps[j].m * f;
      ps[i].vz -= dz * ps[j].m * f;
      ps[j].vx += dx * ps[i].m * f;
      ps[j].vy += dy * ps[i].m * f;
      ps[j].vz += dz * ps[i].m * f;
    }
  }
  for (const p of ps) {
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    p.z += -p.vz * -dt; // (the same value: -a * -b)
  }
}

// lines that are not one per coordinate: each stays as written
function twist(ps: P[]): number {
  let s = 0;
  for (let i = 0; i < ps.length; i++) {
    const a = ps[i].x * 2;
    const b = ps[i].z * 2; // (z, not y: no trio)
    const c = ps[i].y * 2;
    ps[i].vx = ps[i].vy; // (reads another lane: no copy)
    ps[i].vy = ps[i].vz;
    ps[i].vz = a + b + c;
    s += ps[i].vx - ps[i].vy + ps[i].vz;
  }
  return s;
}

function energy(ps: P[]): number {
  let e = 0;
  for (let i = 0; i < ps.length; i++) {
    e += 0.5 * ps[i].m * (ps[i].vx * ps[i].vx + ps[i].vy * ps[i].vy + ps[i].vz * ps[i].vz);
    for (let j = i + 1; j < ps.length; j++) {
      const dx = ps[i].x - ps[j].x;
      const dy = ps[i].y - ps[j].y;
      const dz = ps[i].z - ps[j].z;
      e -= ps[i].m * ps[j].m / Math.sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  return e;
}

function main(): void {
  const ps: P[] = [
    new P(0, 0, 0, 0, 0, 0, 39.47),
    new P(4.84, -1.16, -0.10, 0.606, 2.81, -0.025, 0.037),
    new P(8.34, 4.12, -0.40, -1.01, 1.82, 0.0084, 0.011),
    new P(12.89, -15.11, -0.22, 1.08, 0.868, -0.0108, 0.0017),
    new P(15.37, -25.91, 0.17, 0.979, 0.594, -0.0347, 0.002),
  ];
  console.log(energy(ps).toFixed(12));
  for (let k = 0; k < 2000; k++) step(ps, 0.01);
  console.log(energy(ps).toFixed(12));
  console.log(ps[3].x, ps[3].y, ps[3].z, ps[3].vx, ps[3].vy, ps[3].vz);
  console.log(twist(ps));
  console.log(ps[1].vx, ps[1].vy, ps[1].vz, -0 * ps[0].x);
}

main();
