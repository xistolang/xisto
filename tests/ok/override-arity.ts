// an override with another arity is JavaScript's override: a call through the base (this.upsert(...) in the base's
// code too) runs the subclass's method with the arguments it got (a Record copied into its interface as data)
// build: --tsc-checked
class Repo {
  upsert(filter: string, set: number, setOnInsert: boolean): string {
    return `base ${filter} ${set} ${setOnInsert}`;
  }
  run(): string { return this.upsert('f', 1, true); }
}
class FleetRepo extends Repo {
  upsert(input: string): string {
    return `fleet ${input}`;
  }
}
class Sub2 extends FleetRepo { upsert(input: string): string { return 'sub2 ' + input; } }
const r = new FleetRepo();
console.log(r.upsert('x'), r.run());
const b: Repo = new FleetRepo();
console.log(b.upsert('g', 3, false));
const s2: Repo = new Sub2();
console.log(s2.upsert('z', 1, true), s2.run(), new Repo().run());
interface UpsertInput { nm: string; db: string; extra?: string; }
class BaseSvc<T> {
  async upsert(filter: Record<string, unknown>, set: Record<string, unknown>, setOnInsert?: Record<string, unknown>): Promise<string> {
    return 'base ' + JSON.stringify(filter) + JSON.stringify(set) + (setOnInsert ? 'i' : '-');
  }
  async sync(): Promise<string> { return this.upsert({ nm: 'a', db: 'b' }, { x: 1 }); }
}
class Profile2 { nm: string = ''; }
class ProfileSvc extends BaseSvc<Profile2> {
  async upsert(input: UpsertInput): Promise<string> {
    return 'profile ' + input.nm + '/' + input.db + '/' + (input.extra ?? '-');
  }
}
async function main(): Promise<void> {
  const p = new ProfileSvc();
  console.log(await p.upsert({ nm: 'n', db: 'd' }));
  console.log(await p.sync());
  const b: BaseSvc<Profile2> = p;
  console.log(await b.upsert({ nm: 'q', db: 'r', extra: 'e' }, {}));
  console.log(await new BaseSvc<Profile2>().sync());
}
await main();
