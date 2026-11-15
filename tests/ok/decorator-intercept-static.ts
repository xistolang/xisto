// XB-107: TypeScript method decorators on static methods (recursive, async, void), orig.call(this, ...args), and
// an async void method awaited through its wrapper. Expected: tsc --experimentalDecorators, then Node
const log: string[] = [];
function Wrap(tag: string): MethodDecorator {
  return (target: any, key: string | symbol, descriptor: PropertyDescriptor) => {
    const orig = descriptor.value;
    descriptor.value = function (...args: any[]) {
      log.push(`${tag} ${String(key)}(${args.join(',')})`);
      return orig.call(this, ...args);
    };
    return descriptor;
  };
}
function Later(): MethodDecorator {
  return (target: any, key: string | symbol, descriptor: PropertyDescriptor) => {
    const orig = descriptor.value;
    descriptor.value = async function (...args: any[]) {
      log.push(`start ${String(key)}`);
      const r = await orig.apply(this, args);
      log.push(`end ${String(key)}`);
      return r;
    };
  };
}
class Calc {
  constructor(private readonly k: number) {}
  @Wrap('s')
  static fact(n: number): number {
    return n <= 1 ? 1 : n * Calc.fact(n - 1);
  }
  @Later()
  static async load(id: string): Promise<string> {
    await new Promise((r) => setTimeout(r, 1));
    return `loaded ${id}`;
  }
  @Wrap('v')
  static note(s: string): void {
    log.push(`note ${s}`);
  }
  @Later()
  async save(x: number): Promise<void> {
    await new Promise((r) => setTimeout(r, 1));
    log.push(`saved ${x * this.k}`);
  }
  @Wrap('i')
  scale(x: number): number {
    return x * this.k;
  }
}
async function main(): Promise<void> {
  console.log(Calc.fact(4));
  console.log(await Calc.load('a'));
  Calc.note('hi');
  const c = new Calc(3);
  await c.save(2);
  log.push('after save');
  console.log(c.scale(5));
  const xs = [1, 2].map((v) => c.scale(v));
  console.log(xs.join(' '));
  console.log(log.join('\n'));
}
main();
