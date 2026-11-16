// expect: a class that extends a parameter or a variable (a mixin) is not supported
class Base { hello(): string { return "base"; } }
function Loud(B: typeof Base): unknown {
  return class extends B { shout(): string { return this.hello().toUpperCase(); } };
}
console.log(Loud(Base));
