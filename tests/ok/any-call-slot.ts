// a method called on an any dispatches through its XB-107 slot (a decorator that wrapped it); expected: tsc with experimentalDecorators, then Node
function Log(): MethodDecorator {
  return (target, key, descriptor) => {
    const original = descriptor.value as (...a: unknown[]) => unknown;
    descriptor.value = function (this: unknown, ...args: unknown[]): unknown {
      console.log("before " + String(key));
      return original.apply(this, args);
    };
    return descriptor;
  };
}
class Svc {
  k = 2;
  @Log()
  twice(n: number): number { return n * this.k; }
}
const s: any = new Svc();
console.log(s.twice(21));
