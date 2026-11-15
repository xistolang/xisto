// expect: runtime error: TypeError: Cannot read properties of undefined (reading '0')
// XB-216: new Array<number[]>(3) has holes (null here, undefined in JavaScript, where X[1][0] throws a TypeError);
// the read throws the same TypeError (status 70 when nothing catches it) instead of a segmentation fault
const X: number[][] = new Array<number[]>(3);
X[0] = [1, 2];
console.log(X[0][1]);
console.log(X[1][0]);
