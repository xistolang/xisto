// a generic interface of methods (Contract<I, O>) and an alias of it and of it & { begin(); end() }: the abstract class
// the implementations extend, so a variable of the alias calls the implementation's method
// build: --tsc-checked
interface Contract<I, O> {
  execute(input: I): Promise<O>;
}
type FnContract<I, O> = Contract<I, O>;
interface AIn { n: number }
interface AOut { dobro: number }
type AContract = FnContract<AIn, AOut>;
type BContract = FnContract<AIn, AOut> & {
  begin(): void;
  end(): void;
};
class ALegacy implements AContract {
  async execute(input: AIn): Promise<AOut> { return { dobro: input.n * 2 }; }
}
class AModern implements AContract {
  async execute(input: AIn): Promise<AOut> { return { dobro: input.n + input.n + 1 }; }
}
class BModern implements BContract {
  private cache = 0;
  begin(): void { this.cache = 10; }
  end(): void { this.cache = 0; }
  async execute(input: AIn): Promise<AOut> { return { dobro: input.n * 2 + this.cache }; }
}
async function usa(c: AContract, b: BContract): Promise<void> {
  console.log((await c.execute({ n: 3 })).dobro);
  b.begin();
  console.log((await b.execute({ n: 3 })).dobro);
  b.end();
  console.log((await b.execute({ n: 3 })).dobro);
}
async function main(): Promise<void> {
  await usa(new ALegacy(), new BModern());
  await usa(new AModern(), new BModern());
}
main();
