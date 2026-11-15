// XB-107: method decorators written as TypeScript's that replace descriptor.value with an unannotated
// function (...args: any[]) { orig.apply(this, args) } (its `this` the object it is called on): async and sync,
// with and without factory arguments, two stacked (factories evaluated top-down, applied bottom-up), a static
// method, one that leaves the descriptor as it is. Expected: tsc --experimentalDecorators, then Node (Node runs
// no decorators itself)
const log: string[] = [];
function Trace(name?: string): MethodDecorator {
  return (target: any, key: string | symbol, descriptor: PropertyDescriptor) => {
    const orig = descriptor.value;
    descriptor.value = async function (...args: any[]) {
      const t0 = Date.now();
      try {
        return await orig.apply(this, args);
      } finally {
        log.push(`${String(key)} took ${Date.now() - t0 >= 0} ${name ?? 'anon'}`);
      }
    };
    return descriptor;
  };
}
function Order(tag: string): MethodDecorator {
  log.push(`evaluate ${tag}`);
  return (target: any, key: string | symbol, descriptor: PropertyDescriptor) => {
    log.push(`apply ${tag} to ${String(key)}`);
    const orig = descriptor.value;
    descriptor.value = function (...args: any[]) {
      log.push(`enter ${tag}`);
      const r = orig.apply(this, args);
      log.push(`leave ${tag}`);
      return r;
    };
  };
}
function Plain(target: any, key: string | symbol, descriptor: PropertyDescriptor): void {
  const orig = descriptor.value;
  descriptor.value = function (...args: any[]) {
    return orig.call(this, args[0] + 1, args[1]);
  };
}
function Untouched(): MethodDecorator {
  return (target: any, key: string | symbol, descriptor: PropertyDescriptor) => {
    log.push(`seen ${String(key)}`);
    return descriptor;
  };
}
class Svc {
  base = 10;
  @Trace('get')
  async get(id: number): Promise<number> {
    return id + this.base;
  }
  @Trace()
  async other(s: string): Promise<string> {
    return `${s}${this.base}`;
  }
  @Order('top')
  @Order('bottom')
  sum(a: number, b: number): number {
    log.push(`sum body`);
    return a + b + this.base;
  }
  @Plain
  add(a: number, b: number): number {
    return a + b + this.base;
  }
  @Untouched()
  mul(a: number, b: number): number {
    return a * b * this.base;
  }
  @Order('static')
  static twice(n: number): number {
    return n * 2;
  }
  callsOwn(): number {
    return this.add(1, 1) + this.sum(0, 0);
  }
}
async function main(): Promise<void> {
  const s = new Svc();
  console.log(await s.get(5));
  console.log(await s.other('x'));
  console.log(s.sum(1, 2));
  console.log(s.add(1, 2));
  console.log(s.mul(2, 3));
  console.log(Svc.twice(21));
  s.base = 100;
  console.log(s.callsOwn());
  console.log(log.join('\n'));
}
main();
