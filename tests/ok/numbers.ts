// Number formatting must match ECMAScript Number::toString.
console.log(0, -0, 1, -1, 42, 0.1, 0.5, 1.5, 100, 1e21, 1e20, 123456789012345680000);
console.log(1 / 3, 2 / 3, 0.000001, 0.0000001, 1.5e-7, 1e-7, 1e300, 1e-300);
console.log(1 / 0, -1 / 0, 0 / 0, 3.0, 2.5e3, 0.1 + 0.2, 9007199254740993);
console.log(7 % 3, -7 % 3, 7.5 % 2, Math.round(2.5), Math.round(-2.5), Math.round(0.4));
console.log(Math.sqrt(2), Math.floor(-1.5), Math.ceil(-1.5), Math.abs(-3), Math.min(1, 2), Math.max(1, 2));
console.log(Math.PI, Math.pow(2, 10), Math.trunc(-4.7), 1e21 + 1, 123e-20, 5e-324);
