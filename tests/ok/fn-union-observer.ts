// XB-173: Obs | ((v) => void): an object literal or a function; if (o.next) o.next(v) on the narrowed object
interface Obs {
  next?: (value: number) => void;
  complete?: () => void;
}
function tap(o: Obs | ((value: number) => void), v: number): void {
  if (typeof o === "function") o(v);
  else {
    if (o.next) o.next(v);
    if (o.complete) o.complete();
  }
}
tap((v: number) => console.log("fn", v), 1);
tap({ next: (v: number) => console.log("obj next", v), complete: () => console.log("done") }, 2);
tap({ next: (v) => console.log("untyped next", v) }, 3);
const ob: Obs = { complete: () => console.log("c") };
tap(ob, 4);
