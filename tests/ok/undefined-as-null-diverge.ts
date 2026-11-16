// undefined read as null (XS2101): the cases whose output differs from Node's, pinned in the .expected file
// (xisto's output). Node prints, line by line:
//   false false false undefined undefined
//   false false
//   [ 'name', 'nick' ] { name: 'ana', nick: undefined }
//   [ 1, undefined ] [1,null]
//   {"name":"ana","nick":null} [ 'name', 'nick' ]
// warns: (XS2101
interface User { name: string; nick?: string | null }
let a: string | null = undefined;
const b: string | null = null;
console.log(a === null, b === undefined, typeof b === "undefined", a, typeof a);
a = "x";
console.log(a === null, a === undefined);
const u: User = { name: "ana", nick: "an" };
u.nick = undefined;
console.log(Object.keys(u), u);
const xs: (number | null)[] = [1, undefined];
console.log(xs, JSON.stringify(xs));
const n: User = { name: "ana", nick: null };
console.log(JSON.stringify(n), Object.keys(n));
