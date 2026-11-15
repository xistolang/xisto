// XB-213: instanceof and ArrayBuffer.isView on a value made right there (no variable): the value is let go after the
// test (it leaked), and isView tests every typed array kind on it (a fresh Float64Array was not a view)
class A { x = 1; }
class B extends A { y = 2; }
class C { z = 3; }
function mk(): A { return new B(); }
console.log(ArrayBuffer.isView(new Float64Array(1)));
console.log(ArrayBuffer.isView(new Uint8Array(2)), ArrayBuffer.isView(new Float32Array(2)));
console.log(new B() instanceof A, mk() instanceof B, new A() instanceof B, new C() instanceof C);
console.log(new Float64Array(3) instanceof Float64Array, new Uint8Array(1) instanceof Uint8Array);
let n = 0;
for (let i = 0; i < 1000; i++) if (mk() instanceof B && ArrayBuffer.isView(new Float64Array(i))) n++;
console.log(n);
const kept = new Float32Array(4);
console.log(ArrayBuffer.isView(kept), kept instanceof Float32Array);
