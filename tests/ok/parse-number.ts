// parseInt and parseFloat: radix, 0x, signs, leading space, longest valid prefix, NaN
console.log(parseInt("42"), parseInt("  -17px"), parseInt("0x1F"), parseInt("ff", 16), parseInt("101", 2), parseInt("abc"), parseInt(""), parseInt("12345678901234567890"), parseInt("3.9"), parseInt("z", 36));
console.log(parseFloat("3.14abc"), parseFloat("  -0.5e3x"), parseFloat(".5"), parseFloat("Infinity"), parseFloat("x1"), parseFloat("1e"), parseFloat("-.e1"), parseFloat("0x10"));
