// a thenable whose then() takes no argument it must be given (mongoose's Query, built by a chain and run when
// awaited): `await q` and an element q of Promise.all([...]) are q.then(); exec() runs it the same way
class Query<R> {
  private steps: string[] = [];
  constructor(private readonly value: R, private readonly fail: boolean = false) {}
  limit(n: number): Query<R> {
    this.steps.push('limit ' + n);
    return this;
  }
  async exec(): Promise<R> {
    if (this.fail) throw new Error('no server (' + this.steps.join(', ') + ')');
    return this.value;
  }
  async then(): Promise<R> {
    return await this.exec();
  }
}

class Model {
  count(): Query<number> {
    return new Query<number>(3);
  }
  find(): Query<string[]> {
    return new Query<string[]>(['a', 'b']);
  }
  broken(): Query<string[]> {
    return new Query<string[]>([], true);
  }
}

async function main(): Promise<void> {
  const m = new Model();
  const n = await m.count();
  const xs = await m.find().limit(2);
  console.log(n + 1, xs.length, await m.find().exec());
  const [total, docs] = await Promise.all([m.count(), m.find().limit(5)]);
  console.log(total, docs.join(','));
  try {
    await m.broken().limit(1);
  } catch (e) {
    console.log('rejected', (e as Error).message);
  }
}
main();
