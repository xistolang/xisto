// XB-202: a module variable narrowed by `!== null` that a call sets back to null (a closure, a function or a method
// assigns it): the read after the call is a TypeError, as in JavaScript (it was a segmentation fault)
let last: string | null = "a";
const clear = (): void => { last = null; };
try {
  if (last !== null) { clear(); console.log(last.length); }
} catch (e) {
  console.log("caught", (e as Error).name);
}
let items: number[] | null = [1, 2, 3];
function drop(): void { items = null; }
try {
  if (items !== null) { console.log(items.length); drop(); console.log(items.length); }
} catch (e) {
  console.log("caught", (e as Error).name);
}
class Box { v = 1; }
let box: Box | null = new Box();
class Owner { reset(): void { box = null; } }
function readBox(o: Owner): number {
  if (box === null) return -1;
  o.reset();
  return box.v; // inside a function too
}
try {
  console.log(readBox(new Owner()));
} catch (e) {
  console.log("caught", (e as Error).name);
}
console.log(readBox(new Owner()));
// still null-checked but not reset: the value reads as usual
let kept: string | null = "kept";
const touch = (): void => { if (kept === "x") kept = null; };
if (kept !== null) { touch(); console.log(kept.toUpperCase(), kept.length); }
