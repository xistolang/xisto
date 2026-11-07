// NaN, Infinity and the Number constants and predicates
const xs: number[] = [0, -0, 1.5, NaN, Infinity, -Infinity, Number.MAX_SAFE_INTEGER, Number.MAX_SAFE_INTEGER + 2, 2 ** 53, 1e300 * 1e10];
for (const x of xs) {
  console.log(x, Number.isNaN(x), Number.isFinite(x), Number.isInteger(x), Number.isSafeInteger(x), isNaN(x), isFinite(x));
}
console.log(Number.MIN_SAFE_INTEGER, Number.EPSILON, Number.MAX_VALUE, Number.MIN_VALUE, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY, Number.NaN);
console.log(1 + Number.EPSILON > 1, 0.1 + 0.2 === 0.3, Math.abs(0.1 + 0.2 - 0.3) < Number.EPSILON);
console.log(Number.parseFloat("2.5kg"), Number.parseInt("ff", 16), Number.parseInt("42px"), NaN === NaN, -Infinity < Number.MIN_SAFE_INTEGER);
let best = Infinity;
for (const v of [5, 3, 9]) { best = Math.min(best, v); }
console.log(best, `${NaN}|${Infinity}|${-Infinity}`, String(NaN), JSON.stringify([1]));
console.log(Number.isInteger(5), Number.isSafeInteger(7));
