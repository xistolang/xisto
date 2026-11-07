// The Computer Language Benchmarks Game
// https://salsa.debian.org/benchmarksgame-team/benchmarksgame/
//
// contributed by Ian Osgood
// modified for Node.js by Isaac Gouy
// (xisto: the same program with types; node/spectralnorm-node-1.js is the original)

function A(i: int, j: int): number {
  return 1 / (Number((i + j) * (i + j + 1)) / 2 + Number(i) + 1);
}

function Au(u: number[], v: number[]): void {
  for (let i = 0; i < u.length; ++i) {
    let t = 0;
    for (let j = 0; j < u.length; ++j) {
      t += A(i, j) * u[j];
    }
    v[i] = t;
  }
}

function Atu(u: number[], v: number[]): void {
  for (let i = 0; i < u.length; ++i) {
    let t = 0;
    for (let j = 0; j < u.length; ++j) {
      t += A(j, i) * u[j];
    }
    v[i] = t;
  }
}

function AtAu(u: number[], v: number[], w: number[]): void {
  Au(u, w);
  Atu(w, v);
}

function spectralnorm(n: int): number {
  const u: number[] = [], v: number[] = [], w: number[] = [];
  let vv = 0, vBv = 0;
  for (let i = 0; i < n; ++i) {
    u.push(1);
    v.push(0);
    w.push(0);
  }
  for (let i = 0; i < 10; ++i) {
    AtAu(u, v, w);
    AtAu(v, u, w);
  }
  for (let i = 0; i < n; ++i) {
    vBv += u[i] * v[i];
    vv += v[i] * v[i];
  }
  return Math.sqrt(vBv / vv);
}

console.log(spectralnorm(Math.trunc(Number(process.argv[2]))).toFixed(9));
