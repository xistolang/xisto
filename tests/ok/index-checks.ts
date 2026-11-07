// index checks a counted loop already makes (emit.c, bce_enter): dropped only when nothing in the body can change
// the array's length; every other case keeps its check and still reports an out-of-bounds index. Checked
// against the output without the optimization (XISTO_NO_BCE=1): an out-of-bounds read is an error in xisto.
const xs: int[] = [5, 6, 7, 8];
let sum: int = 0;
for (let i: int = 0; i < xs.length; i++) {
  xs[i] = xs[i] * 2;
  sum += xs[i];
}
console.log("safe", sum, xs.join(","));

// another name for the same array shrinks it: checked, and the error is caught
const ys: int[] = [1, 2, 3, 4];
const alias: int[] = ys;
try {
  for (let i: int = 0; i < ys.length; i++) {
    if (i === 1) {
      alias.pop();
      alias.pop();
      alias.pop();
    }
    sum += ys[i];
  }
} catch (e) {
  console.log("alias caught:", e.message);
}

// a user function in the body could change it: checked
function shrink(a: int[]): void {
  a.pop();
}
const zs: int[] = [1, 2, 3];
try {
  for (let i: int = 0; i < zs.length; i++) {
    shrink(zs);
    sum += zs[i];
  }
} catch (e) {
  console.log("call caught:", e.message);
}

// a negative start: checked
const ws: int[] = [9, 9];
try {
  for (let i: int = -1; i < ws.length; i++) {
    sum += ws[i];
  }
} catch (e) {
  console.log("negative caught:", e.message);
}

// strings and nested loops
const names: string[] = ["ana", "bia", "cid"];
let s: string = "";
for (let i: int = 0; i < names.length; i++) {
  for (let j: int = 0; j < names.length; j++) {
    if (i !== j) {
      s = s + names[i].slice(0, 1) + names[j].slice(0, 1) + " ";
    }
  }
}
console.log(s.trim(), sum);
