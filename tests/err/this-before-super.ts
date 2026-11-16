// expect: the constructor of D must call super(...) first
// XB-153: this before super(...), even inside an arrow
class Base { v = 1; }
class D extends Base {
  w: number;
  constructor() {
    const f = () => this.v;
    super();
    this.w = f();
  }
}
console.log(new D().w);
