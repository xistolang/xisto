// super.x through a base's getter and setter (also from an arrow), super.m() and super.x in static members (with
// this the class the static was called on, a subclass that inherits it too), static get/set (inherited too).
class A {
  static count = 1;
  protected _n = "a";
  get name(): string { return "A:" + this._n; }
  set name(v: string) { this._n = v; }
  greet(): string { return "hi " + this.name; }
  static kind(): string { return "A-kind"; }
  static who(): string { return "who=" + this.name; }
  static get label(): string { return "A-label:" + this.name; }
  static set label(v: string) { console.log("A set", v); }
}
class B extends A {
  get name(): string { return "B<" + super.name + ">"; }
  set name(v: string) { super.name = v + "!"; }
  greet(): string { return super.greet() + "!"; }
  static kind(): string { return "B/" + super.kind(); }
  static who(): string { return "B:" + super.who() + ":" + super.count; }
  static get label(): string { return "B/" + super.label; }
  static set label(v: string) { super.label = v + "?"; }
  both(): string { const f = (): string => super.name; return f() + "|" + super.greet(); }
}
class C extends B {}
const b = new B();
console.log(b.name, b.greet(), B.kind(), B.label, b.both());
b.name = "z";
console.log(b.name);
console.log(A.who(), B.who(), C.who(), C.label, A.label);
B.label = "v";
C.label = "w";
