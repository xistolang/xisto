// Math: the rest of the functions and constants, imul/clz32 on 32-bit integers, and ** (int and number)
console.log(Math.E, Math.LN2, Math.LN10, Math.LOG2E, Math.LOG10E, Math.SQRT2, Math.SQRT1_2);
console.log(Math.log2(1024), Math.log10(1000), Math.log1p(0), Math.expm1(0), Math.cbrt(27), Math.sign(-3.5), Math.sign(0), Math.sign(2));
console.log(Math.asin(1), Math.acos(1), Math.atan(1), Math.atan2(1, 1), Math.hypot(3, 4), Math.sinh(0), Math.cosh(0), Math.tanh(0));
console.log(Math.asinh(0), Math.acosh(1), Math.atanh(0), Math.fround(5.5), Math.fround(5.05));
console.log(Math.imul(3, 4), Math.imul(0xffffffff, 5), Math.imul(0x7fffffff, 2), Math.clz32(1), Math.clz32(0), Math.clz32(-1));
const k: int = 10;
console.log(2 ** 10, 2 ** k, 2 ** 0.5, 10 ** -2, (-2) ** 3, 2 ** 3 ** 2);
