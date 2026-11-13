// Int32Array on a SharedArrayBuffer and Atomics (runtime xs_atomics_*): the operations and their answers as Node's,
// wait / notify between the main thread and a worker (a barrier of 50 rounds, without messages)
import { Worker, isMainThread, parentPort } from "node:worker_threads";

function mainThread(): void {
  const sab = new SharedArrayBuffer(16);
  const a = new Int32Array(sab);
  console.log(a.length, Atomics.load(a, 0));
  console.log(Atomics.store(a, 1, 7), Atomics.add(a, 1, 5), Atomics.load(a, 1));
  console.log(Atomics.sub(a, 1, 2), Atomics.exchange(a, 1, 100), Atomics.compareExchange(a, 1, 100, 3), Atomics.compareExchange(a, 1, 99, 4), Atomics.load(a, 1));
  console.log(Atomics.add(a, 1, 2147483647), Atomics.load(a, 1)); // wraps as an int32
  Atomics.store(a, 1, 3);
  console.log(Atomics.wait(a, 2, 1)); // not the value
  console.log(Atomics.wait(a, 2, 0, 10)); // nobody notifies
  console.log(Atomics.notify(a, 2), Atomics.notify(a, 2, 1));
  const own = new Int32Array(3);
  console.log(own.length, Atomics.add(own, 0, 4), Atomics.load(own, 0));
  const w = new Worker(__filename);
  w.postMessage(sab);
  for (let round = 1; round <= 50; round++) {
    Atomics.store(a, 0, round); // go
    Atomics.notify(a, 0);
    while (Atomics.load(a, 3) !== round) Atomics.wait(a, 3, round - 1); // until the worker is done with it
  }
  console.log("sum", Atomics.load(a, 2), "rounds", Atomics.load(a, 3));
  console.log(a);
  w.terminate();
}

function workerThread(): void {
  parentPort.on("message", (sab: SharedArrayBuffer) => {
    const a = new Int32Array(sab);
    for (let round = 1; round <= 50; round++) {
      while (Atomics.load(a, 0) !== round) Atomics.wait(a, 0, round - 1);
      Atomics.add(a, 2, round);
      Atomics.store(a, 3, round);
      Atomics.notify(a, 3);
    }
  });
}

if (isMainThread) mainThread();
else workerThread();
