// x % y on numbers: the integer fast path of xs_fmod must keep fmod's results, the dividend's sign and -0
const xs: number[] = [7, -7, 0, -0, 4, -4, 7.5, -7.5, 1e300, 9007199254740993, NaN, Infinity, -Infinity, 5];
const ys: number[] = [3, -3, 2, Infinity, 0, -2, 0.5, 2.5];
let out = "";
for (const x of xs) for (const y of ys) { const r = x % y; out += (r === 0 && 1 / r < 0) ? "-0 " : `${r} `; }
console.log(out);
