// static v: T; without a value, set by a static block or a static method (xisto starts it at T's empty value:
// 0, "", false, null, []; Node at undefined: this test does not read it before it is set)
class A {
  static v: number;
  static s: string;
  static readonly list: number[];
  static cfg: { port: number };
  static {
    A.v = 42;
    A.s = "x";
    A.list = [1, 2];
    A.cfg = { port: 8080 };
  }
  static bump(): number { return ++A.v; }
}
console.log(A.v, A.s, A.list, A.cfg.port, A.bump());
class B {
  static count: number;
  static inc(): void { B.count = (B.count || 0) + 1; }
}
B.inc();
B.inc();
console.log(B.count);
