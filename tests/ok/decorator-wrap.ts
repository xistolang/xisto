// XB-107: method decorators written as TypeScript's, (target, key, descriptor), that replace descriptor.value with
// function (this: T, ...args) { ... original.apply(this, args) }: logging before/after, changing arguments and result,
// an async method, two stacked (bottom up), a wrapper declared as a nested function; calls from outside and this.m()
// all go through the wrapper. Expected: ts-node with experimentalDecorators (Node runs no decorators itself)
// build: --tsc-checked
const log: string[] = [];
function isPromise<T = unknown>(value: unknown): value is Promise<T> {
  return Boolean(value) && typeof (value as { then?: unknown }).then === 'function';
}
function Logged(tag: string): MethodDecorator {
  return (target: any, propertyKey: string | symbol, descriptor: PropertyDescriptor) => {
    const original = descriptor.value as (...args: unknown[]) => unknown;
    const name = String(propertyKey);
    descriptor.value = function (this: unknown, ...args: unknown[]): unknown {
      log.push(`${tag} before ${name}(${args.join(',')})`);
      try {
        const r = original.apply(this, args);
        if (isPromise(r)) {
          return r.then(
            (v) => {
              log.push(`${tag} after ${name} = ${String(v)}`);
              return v;
            },
            (err: unknown) => {
              log.push(`${tag} failed ${name}: ${(err as Error).message}`);
              throw err;
            },
          );
        }
        log.push(`${tag} after ${name} = ${String(r)}`);
        return r;
      } catch (err) {
        log.push(`${tag} failed ${name}: ${(err as Error).message}`);
        throw err;
      }
    };
    return descriptor;
  };
}
function Dobra(): MethodDecorator {
  return (target: any, propertyKey: string | symbol, descriptor: PropertyDescriptor) => {
    const original = descriptor.value as (...args: unknown[]) => unknown;
    descriptor.value = function (this: unknown, ...args: unknown[]): unknown {
      const novos = args.map((a) => (typeof a === 'number' ? a * 2 : a));
      const r = original.apply(this, novos);
      return typeof r === 'number' ? r + 1 : r;
    };
  };
}
function Contado(): MethodDecorator & ClassDecorator {
  return ((target: object, propertyKey?: string | symbol, descriptor?: PropertyDescriptor) => {
    if (!descriptor || typeof descriptor.value !== 'function') return descriptor;
    const original = descriptor.value as (...args: unknown[]) => unknown;
    let chamadas = 0;
    function wrapped(this: unknown, ...args: unknown[]): unknown {
      chamadas++;
      log.push(`contado ${String(propertyKey)} #${chamadas}`);
      return original.apply(this, args);
    }
    Object.defineProperty(wrapped, 'name', { value: String(propertyKey), configurable: true });
    descriptor.value = wrapped;
    return descriptor;
  }) as MethodDecorator & ClassDecorator;
}
class Conta {
  @Contado()
  triplo(n: number): number {
    return n * 3 + this.base;
  }
  base = 100;
  @Logged('L')
  soma(a: number, b: number): number {
    return this.base + a + b;
  }
  @Dobra()
  vezes(a: number): number {
    return a * 10;
  }
  @Logged('outer')
  @Logged('inner')
  nome(p: string): string {
    return `${p}-${this.base}`;
  }
  @Logged('A')
  async carrega(id: number): Promise<string> {
    await new Promise((r) => setTimeout(r, 1));
    if (id < 0) throw new Error('negativo');
    return 'item' + id;
  }
  usaInterno(): number {
    return this.soma(1, 2) + this.vezes(3);
  }
}
async function main(): Promise<void> {
  const c = new Conta();
  console.log(c.soma(1, 2));
  console.log(c.vezes(4));
  console.log(c.nome('x'));
  console.log(c.usaInterno());
  console.log(c.triplo(2), c.triplo(3));
  console.log(await c.carrega(7));
  try {
    await c.carrega(-1);
  } catch (e) {
    console.log('erro', (e as Error).message);
  }
  console.log(log.join('\n'));
}
main();
