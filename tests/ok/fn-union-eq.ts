// XB-173: === on unions holding functions: the same function through the adapters (xs_fn_origin)
function named(n: number): number { return n; }
const u: string | ((n: number) => number) = named;
const v: string | ((n: number) => number) = named;
console.log(u === v, u === "x");
const w: string | ((n: number) => number) = (n: number) => n;
console.log(u === w);
console.log(String(typeof w));
console.log(`${typeof u}`);
type Tap<T> = { next?: (v: T) => void; error?: (e: string) => void } | ((v: T) => void);
function tap<T>(o: Tap<T>, v: T): void {
  const fn = typeof o === "function" ? o : o.next;
  if (fn) fn(v);
}
tap((v: number) => console.log("t", v), 1);
tap({ next: (v: number) => console.log("n", v) }, 2);
tap({ error: (e: string) => console.log(e) }, 3);
