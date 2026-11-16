// '?:' where an object goes, one branch an object literal (or an interface value) and the other a value narrowed to
// object by typeof: an object, as the if/else form (it was XS8143: the branches were {anonymous} and object)
interface Msg { message: string }
function i6(x: string | object): object { if (typeof x === "string") return { message: x }; else return x; }
function c6(x: string | object): object { return typeof x === "string" ? { message: x } : x; }
function c7(x: string | object, m: Msg): object { return typeof x === "string" ? m : x; }
function c8(x: string | object | null): object | null { return x === null ? null : typeof x === "string" ? { message: x } : x; }
console.log(JSON.stringify(i6("s")), JSON.stringify(c6("t")), JSON.stringify(c7("u", { message: "m" })));
console.log(JSON.stringify(c8(null)), JSON.stringify(c8("v")));
