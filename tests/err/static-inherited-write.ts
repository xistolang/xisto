// expect: B.count is a static field of A: writing it through B would give B a field of its own
class A { static count = 0; }
class B extends A {}
B.count = 3;
console.log(A.count);
