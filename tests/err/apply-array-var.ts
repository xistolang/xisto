// expect: s.apply takes its arguments as an array literal here
function s(a: number): number { return a; }
const xs: number[] = [1];
console.log(s.apply(null, xs));
