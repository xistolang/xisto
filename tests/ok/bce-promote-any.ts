// an invariant slot in a register when it is not the first slot the body writes (emit.c, bce_promo_plan with
// bce_promo_body_): bicg's s[j] = s[j] + r[i] * A[i][j]; q[i] = q[i] + A[i][j] * p[j] keeps q[i] in a C local for
// the j loop. Every case where q[i] can be reached another way (q is s, p, r or a row of A) must fall back to the
// loop as written and print what Node prints.
function bicg(m: int, n: int, A: number[][], s: number[], q: number[], p: number[], r: number[]): void {
  for (let i = 0; i < m; i++) s[i] = 0;
  for (let i = 0; i < n; i++) {
    q[i] = 0.0;
    for (let j = 0; j < m; j++) {
      s[j] = s[j] + r[i] * A[i][j];
      q[i] = q[i] + A[i][j] * p[j];
    }
  }
}
// two invariant slots after a moving one: the first of them is kept, the other stays in memory
function two(n: int, m: int, A: number[][], s: number[], q: number[], t: number[]): void {
  for (let i = 0; i < n; i++)
  for (let j = 0; j < m; j++) {
    s[j] += A[i][j];
    q[i] = q[i] * 0.5 + A[i][j];
    t[i] -= q[i] * 0.25;
  }
}
function grid(r: int, c: int, f: (i: int, j: int) => number): number[][] {
  const g: number[][] = [];
  for (let i = 0; i < r; i++) {
    const row: number[] = [];
    for (let j = 0; j < c; j++) row.push(f(i, j));
    g.push(row);
  }
  return g;
}
function show(v: number[]): string {
  return v.map((x) => x.toFixed(6)).join(",");
}
function vec(n: int, k: int): number[] {
  const v: number[] = [];
  for (let i = 0; i < n; i++) v.push(((i * k) % 7) / 7 + 0.125);
  return v;
}

// m = 5 columns, n = 6 rows (m < n, as in PolyBench)
const A = grid(6, 5, (i, j) => ((i * (j + 1)) % 6) / 6);
const s = vec(5, 1), q = vec(6, 2), p = vec(5, 3), r = vec(6, 4);
bicg(5, 6, A, s, q, p, r);
console.log("bicg", show(s), "|", show(q));
// q is s: q[i] is also s[i], written by the first statement in the same loop
const sq = vec(6, 5);
bicg(5, 6, grid(6, 5, (i, j) => (i + j) / 11), sq, sq, vec(5, 2), vec(6, 3));
console.log("q = s", show(sq));
// q is p: q[i] is read as p[j] when j = i
const qp = vec(6, 3);
bicg(5, 6, grid(6, 5, (i, j) => (i * 2 + j) / 13), vec(5, 1), qp, qp, vec(6, 6));
console.log("q = p", show(qp));
// q is r: r[i] is q[i], zeroed before the loop and read by the first statement
const qr = vec(6, 4);
bicg(5, 6, grid(6, 5, (i, j) => (j + 1) / (i + 2)), vec(5, 5), qr, vec(5, 6), qr);
console.log("q = r", show(qr));
// q is a row of A: A[i][j] with j = i is q[i]
const B = grid(6, 6, (i, j) => ((i + 2 * j) % 5) / 5);
bicg(6, 6, B, vec(6, 1), B[2], vec(6, 2), vec(6, 3));
console.log("q = A[2]", B.map(show).join(" "));

const s2 = vec(4, 2), q2 = vec(3, 3), t2 = vec(3, 5);
two(3, 4, grid(3, 4, (i, j) => (i * 4 + j) / 12), s2, q2, t2);
console.log("two", show(s2), "|", show(q2), "|", show(t2));
// t is q: the second invariant slot is the first one
const qt = vec(3, 6);
two(3, 4, grid(3, 4, (i, j) => (i + j + 1) / 7), vec(4, 1), qt, qt);
console.log("two t = q", show(qt));
