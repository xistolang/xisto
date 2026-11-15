// XB-183: timers with the same deadline fire in the order they were created, as Node's (FIFO): tasks that each wait
// on setTimeout(r, 1), timers created by other timers' callbacks, and by promise reactions that take different times
function wait(ms: number): Promise<void> { return new Promise<void>((r) => { setTimeout(r, ms); }); }
async function w(id: int, n: int): Promise<void> {
  for (let k = 0; k < n; k++) {
    await wait(1);
    console.log("w", id, k);
  }
}
async function main(): Promise<void> {
  const ps: Promise<void>[] = [];
  for (let i = 0; i < 6; i++) ps.push(w(i, 3));
  await Promise.all(ps);
  // created in different tasks: timer callbacks that each start another timer
  const order: int[] = [];
  await new Promise<void>((done) => {
    let left = 6;
    for (let i = 0; i < 6; i++) {
      setTimeout(() => {
        setTimeout(() => { order.push(i); left--; if (left === 0) done(); }, 1);
      }, i % 2 === 0 ? 1 : 2);
    }
  });
  console.log(order.join(","));
  const o2: int[] = [];
  await new Promise<void>((done) => {
    let left = 3;
    for (let i = 0; i < 3; i++) {
      Promise.resolve(0).then(() => {
        let x = 0;
        for (let j = 0; j < 200000 * (3 - i); j++) x += j % 7;
        setTimeout(() => { o2.push(i * 2); left--; if (left === 0) done(); }, 1);
        return x;
      });
    }
  });
  console.log(o2.join(","));
}
main();
