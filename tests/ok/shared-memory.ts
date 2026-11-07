// SharedArrayBuffer and Float64Array (runtime/xisto_rt.h), and through node:worker_threads: a buffer posted to a
// worker is the same memory there (each side writes, the other reads after a message); a Float64Array of its own
// is posted as a copy
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

function sum(f: Float64Array): number {
  let s = 0;
  for (let i = 0; i < f.length; i++) s += f[i];
  return s;
}

function local(): void {
  const f = new Float64Array(4);
  f[0] = 1.5;
  f[1] = -0;
  f[2] += 2;
  f[3] = f[2] * 10;
  f[3]++;
  console.log(f, f.length, f.byteLength, f.byteOffset);
  console.log(f.join(), String(f.toString()), f.join(" | "));
  const g = new Float64Array([0.1, 0.2, 0.3]);
  let t = 0;
  for (const x of g) t += x;
  console.log(t, sum(g), new Float64Array([1, 2, 3]).length);
  const h = g.slice(1);
  h[0] = 9;
  const v = g.subarray(1, 3);
  v[1] = 7;
  console.log(g, h, v, v.byteOffset);
  g.fill(4).fill(5, 2);
  g.set([8], 1);
  console.log(g, new Float64Array(g).join());
  const sab = new SharedArrayBuffer(32);
  const a = new Float64Array(sab);
  const b = new Float64Array(sab, 8, 2);
  b[0] = 3.25;
  console.log(a, b, sab.byteLength, a[1] === 3.25);
  console.log(new SharedArrayBuffer(4), new Float64Array(0), new SharedArrayBuffer(0));
  // a counted loop over a Float64Array that calls user code: the checks go (the length is fixed), the result stays
  const big = new Float64Array(1000);
  for (let i = 0; i < big.length; i++) big[i] = i * 0.5;
  let acc = 0;
  for (let i = 0; i < 1000; i++) acc += big[i] * twice(i);
  console.log(acc);
}

function twice(i: int): number {
  return Number(i) * 2;
}

if (isMainThread) {
  local();
  const sab = new SharedArrayBuffer(8 * 4);
  const f = new Float64Array(sab);
  for (let i = 0; i < 4; i++) f[i] = i + 1;
  const w = new Worker(__filename, { workerData: "shared" });
  w.on("message", (m: string) => {
    console.log("worker saw", m, "and doubled:", f.join());
    w.terminate();
    // a Float64Array of its own goes as a copy; one on a SharedArrayBuffer, as a view (here on the last two)
    const c = new Worker(__filename, { workerData: "copy" });
    const own = new Float64Array([10, 20]);
    let left = 2;
    c.on("message", (r: Float64Array) => {
      console.log("back", r, own[0]);
      left--;
      if (left === 0) {
        console.log("shared:", f.join());
        c.terminate();
      }
    });
    c.postMessage(own);
    own[0] = -1;
    c.postMessage(f.subarray(2));
  });
  w.postMessage(sab);
} else if (workerData === "shared") {
  parentPort.on("message", (m: SharedArrayBuffer) => {
    const f = new Float64Array(m);
    let s = 0;
    for (let i = 0; i < f.length; i++) {
      s += f[i];
      f[i] *= 2;
    }
    parentPort.postMessage(`${s}`);
  });
} else if (workerData === "copy") {
  parentPort.on("message", (m: Float64Array) => {
    m[m.length - 1] = 99;
    parentPort.postMessage(m);
  });
}
