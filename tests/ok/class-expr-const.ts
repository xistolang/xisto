// const K = class { ... } at module level: the class K (its statics, new K(), K.name, a base)
const K = class { static tag = 1; v = 2; static twice(): number { return this.tag * 2; } };
class Base { b = "base"; }
const Sub = class extends Base { s = "sub"; };
console.log(K.tag, K.twice(), K.name);
console.log(new K().v);
const o = new Sub();
console.log(o.b, o.s, o instanceof Base, o instanceof Sub);
