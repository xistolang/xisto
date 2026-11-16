// expect: xisto: 6 errors (XS8018 3, XS3013 1, XS9031 1, XS9033 1)
// XB-113: a build reports every independent error (declarations, bodies, top level, a declaration that does not
// parse), not the first one, and the uses of what failed are not reported again
function f(x: Unknwn): number { return 1; }
class D extends NoBase { a: number = 1; }
function g(): number { return "bad"; }
function h( {
}
const v = f(3) + g();
const d = new D();
const s: string = 5;
let q: number = g() + "x";
console.log(d.a, v, s, q, h);
