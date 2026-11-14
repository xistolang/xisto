// expect: runtime error: null reference: read of an element that was never set
// XB-216: new Array<number[]>(3) has holes (null here, undefined in JavaScript, where X[1][0] throws a TypeError);
// the read says so (status 70) instead of a segmentation fault
const X: number[][] = new Array<number[]>(3);
X[0] = [1, 2];
console.log(X[0][1]);
console.log(X[1][0]);
