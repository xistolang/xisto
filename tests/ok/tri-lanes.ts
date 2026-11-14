// the rows of a triangular elimination in lanes (emit.c, tr_emit_loop): the first half of lu and ludcmp (j < i) for U
// rows together below the first one's row, then each row's tail and its second half in order; the same operations
// on the same values. Rows left over, a local left as the program leaves it, and the guards: a row twice, a later
// row read by the rest (not this form)
function lu(n: int, A: number[][]): void {
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < i; j++) {
      for (let k = 0; k < j; k++) {
        A[i][j] -= A[i][k] * A[k][j];
      }
      A[i][j] /= A[j][j];
    }
    for (let j = i; j < n; j++) {
      for (let k = 0; k < i; k++) {
        A[i][j] -= A[i][k] * A[k][j];
      }
    }
  }
}
function ludcmp(n: int, A: number[][]): number {
  let w = 0.0;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < i; j++) {
      w = A[i][j];
      for (let k = 0; k < j; k++) {
        w -= A[i][k] * A[k][j];
      }
      A[i][j] = w / A[j][j];
    }
    for (let j = i; j < n; j++) {
      w = A[i][j];
      for (let k = 0; k < i; k++) {
        w -= A[i][k] * A[k][j];
      }
      A[i][j] = w;
    }
  }
  return w;
}
// the rest reads the next row: rows of a group would see another lane's work, so the loop stays as written
function peek(n: int, A: number[][]): void {
  for (let i = 0; i < n - 1; i++) {
    for (let j = 0; j < i; j++) {
      for (let k = 0; k < j; k++) A[i][j] -= A[i][k] * A[k][j] * 0.5;
      A[i][j] /= A[j][j];
    }
    for (let j = i; j < n; j++) A[i][j] += A[i + 1][j] * 0.25;
  }
}
function grid(n: int, s: int): number[][] {
  const g: number[][] = [];
  for (let i = 0; i < n; i++) {
    const r: number[] = [];
    for (let j = 0; j < n; j++) r.push(i === j ? n + 1.0 + s : ((i * 7 + j * 3 + s) % 11) / 10.0);
    g.push(r);
  }
  return g;
}
function sum(g: number[][]): number {
  let s = 0.0;
  for (const r of g) for (let j = 0; j < r.length; j++) s = s * 0.75 + r[j];
  return s;
}
const ns: int[] = [3, 9, 21, 30];
for (const n of ns) {
  const A = grid(n, 1);
  lu(n, A);
  const B = grid(n, 2);
  const w = ludcmp(n, B);
  console.log(n, sum(A), sum(B), w);
}
let C = grid(21, 3);
C.reverse();
lu(21, C);
console.log("reversed", sum(C));
C = grid(21, 3);
C[12] = C[4];
lu(21, C);
console.log("twice", sum(C));
C = grid(21, 4);
peek(21, C);
console.log("peek", sum(C));
