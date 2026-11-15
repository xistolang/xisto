// XB-173: tap<T>(Partial<Observer<T>> | ((v: T) => void)): T inferred from the function or from the object
interface Observer<T> {
  next: (value: T) => void;
  complete: () => void;
}
function tap1<T>(o: Partial<Observer<T>>, v: T): void {
  if (o.next) o.next(v);
}
tap1({ next: (v: number) => console.log("obj next", v) }, 2);
function tap2<T>(o: Partial<Observer<T>> | ((v: T) => void), v: T): void {
  if (typeof o === "function") o(v);
  else if (o.next) o.next(v);
}
tap2({ next: (v: number) => console.log("obj next", v) }, 3);
tap2((v: string) => console.log("fn", v), "s");
