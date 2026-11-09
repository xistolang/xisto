// distribute.c: a pair loop over columns whose body starts with pure slow consts runs them first for every pair
class P {
  x: number; y: number; v: number; w: number;
  constructor(x: number, y: number, v: number, w: number) {
    this.x = x;
    this.y = y;
    this.v = v;
    this.w = w;
  }
}

// fires: the consts read x, y, w (never written in the loop) and dt; the stores write v
function step(ps: P[], dt: number): void {
  const n = ps.length;
  for (let i = 0; i < n; i++) {
    for (let j = i + 1; j < n; j++) {
      const dx = ps[i].x - ps[j].x;
      const dy = ps[i].y - ps[j].y;
      const d2 = dx * dx + dy * dy + 0.5;
      const mag = dt / (d2 * Math.sqrt(d2));
      const wj = ps[j].w * mag;
      ps[i].v -= dx * wj;
      const wi = ps[i].w * mag;
      ps[j].v += dy * wi;
    }
  }
  for (const p of ps) p.x += p.v * dt;
}

// does not fire: the const reads v, which the loop writes (the second pair must see the first one's store)
function chained(ps: P[]): void {
  const n = ps.length;
  for (let i = 0; i < n; i++) {
    for (let j = i + 1; j < n; j++) {
      const s = Math.sqrt(ps[i].v * ps[i].v + 1) / (ps[j].w + 2);
      ps[j].v += s;
    }
  }
}

// does not fire: nothing slow in the consts
function cheap(ps: P[]): number {
  let e = 0;
  const n = ps.length;
  for (let i = 0; i < n; i++) {
    for (let j = i + 1; j < n; j++) {
      const d = ps[i].x - ps[j].x;
      e += d * d;
    }
  }
  return e;
}

function main(): void {
  const ps = [new P(1, 2, 0.1, 3), new P(-4, 0.5, 0, 1.5), new P(2.5, -3, -0.2, 2), new P(0, 0, 0.3, 0.25),
    new P(7, 1, 0, 9), new P(-1, -1, 0.05, 0.75)];
  for (let k = 0; k < 1000; k++) step(ps, 0.01);
  chained(ps);
  console.log(cheap(ps).toFixed(12));
  for (const p of ps) console.log(p.x, p.y, p.v, p.w);
  console.log(1 / (ps[3].v * 0) < 0 ? "-0 kept" : "+0");
}
main();
