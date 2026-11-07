const xs: number[] = [3, 1, 2];
const ks: int[] = [3, 1, 2];
const flags: boolean[] = new Array<boolean>(3).fill(true);
flags[1] = false;
console.log(xs, ks, flags, xs.length, flags.length);

function sum(a: number[]): number {
  let s: number = 0;
  for (let i: int = 0; i < a.length; i++) {
    s += a[i];
  }
  return s;
}

const big: number[] = new Array<number>(1000).fill(0.5);
xs[0] += 10;
xs[2]++;
console.log(sum(xs), sum(big), xs[0], xs[2]);
const empty: number[] = [];
console.log(empty, empty.length);
