// an async call not awaited whose body never waits runs to its end at the call, as in Node; xisto makes no task and
// no promise for it (a plain call: the cost of the call before floating calls were started as tasks)
// warns: XS2112 3
let count = 0;
async function bump(n: number): Promise<void> {
  count += n;
}
async function twice(n: number): Promise<number> {
  return n * 2;
}
class Counter {
  total = 0;
  async add(n: number): Promise<void> {
    this.total += n;
  }
}

const c = new Counter();
for (let i = 0; i < 100000; i++) {
  bump(1);
  twice(i);
  c.add(2);
}
console.log(count, c.total);
