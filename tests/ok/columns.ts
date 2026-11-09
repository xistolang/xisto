// A local array of objects of a class made only of numbers, used only through its fields, becomes one
// Float64Array per field (columns.c); everywhere else the objects stay. The output is the same either way.

class P {
  x: number; y: number; w: number;
  constructor(x: number, y: number, w: number) {
    this.x = x;
    this.y = y;
    this.w = w;
  }
}

function step(ps: P[], dt: number): void {
  const n = ps.length;
  for (let i = 0; i < n; i++) {
    for (let j = i + 1; j < n; j++) {
      const dx = ps[i].x - ps[j].x;
      const dy = ps[i].y - ps[j].y;
      const d = Math.sqrt(dx * dx + dy * dy) + 0.1;
      ps[i].w -= d * dt;
      ps[j].w += d * dt / 3;
    }
  }
  for (const p of ps) {
    p.x += p.w * dt;
    p.y -= p.w * dt * 0.5;
  }
}

function total(ps: P[]): number {
  let t = 0;
  for (let i = 0; i < ps.length; i++) {
    const p = ps[i];
    t += p.x * 0.25 + p.y * 0.5 + p.w;
  }
  const first = ps[0];
  return t + first.x * -0.0 + ps[2].y;
}

let sideEffects = 0;
function arg(v: number): number {
  sideEffects = sideEffects * 10 + 1;
  return v;
}

function columns(): void {
  // applies: literal of new P, used only through fields and two functions
  const ps: P[] = [new P(arg(1), arg(2), arg(0.5)), new P(-0.0, 3, 1), new P(4.5, -1, 2), new P(0.1, 0.2, -0.3)];
  console.log(1 / ps[1].x, 1 / total(ps)); // -0 kept in its column
  for (let s = 0; s < 1000; s++) step(ps, 0.001);
  console.log(total(ps).toFixed(12), ps.length, sideEffects);
}

// does not apply: the array is printed (used as a value)
function printed(): void {
  const qs: P[] = [new P(1, 2, 3), new P(4, 5, 6)];
  qs[1].w += 0.5;
  console.log(qs[0].x + qs[1].w, qs.length);
  console.log(qs);
}

// does not apply: an index the rule cannot prove in range (a parameter)
function at(rs: P[], k: int): number {
  return rs[k].x;
}
function indexed(): void {
  const rs: P[] = [new P(7, 8, 9), new P(10, 11, 12)];
  console.log(at(rs, 1), at(rs, 0));
}

// does not apply: an element kept in another variable
function kept(): void {
  const ks: P[] = [new P(1, 1, 1), new P(2, 2, 2)];
  const all: P[] = [];
  for (const k of ks) all.push(k);
  all[0].x = 9;
  console.log(ks[0].x, all.length);
}

// does not apply: a closure reads it
function closure(): void {
  const cs: P[] = [new P(1, 2, 3), new P(4, 5, 6), new P(7, 8, 9)];
  const sum = (i: int): number => cs[i].x + cs[i].y;
  console.log(sum(0) + sum(2));
}

columns();
printed();
indexed();
kept();
closure();
