// Uint8Array on a SharedArrayBuffer (runtime/xisto_rt.h: xs_u8_on): a view on the shared bytes, posted to a
// worker as the same memory (runtime/xisto_net.h: xs_wq_put_u8); bytes of their own still go as a copy
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

function local(): void {
  const sab = new SharedArrayBuffer(12);
  const a = new Uint8Array(sab);
  const b = new Uint8Array(sab, 4, 4);
  const c = new Uint8Array(sab, 8);
  for (let i = 0; i < a.length; i++) a[i] = i * 3;
  b[0] = 200;
  c[3] = 255;
  console.log(a, b, c);
  console.log(a.length, a.byteLength, a.byteOffset, b.byteOffset, c.byteOffset, c.length, sab.byteLength);
  const v = a.subarray(5, 9);
  v[0] = 7;
  console.log(v, v.byteOffset, a[5], new Uint8Array(4).byteOffset, new Uint8Array(6).subarray(2).byteOffset);
  let s: int = 0;
  for (const x of b) s += x;
  console.log(s, b.join("-"), new Uint8Array(new SharedArrayBuffer(0)).length);
}

if (isMainThread) {
  local();
  const sab = new SharedArrayBuffer(8);
  const shared = new Uint8Array(sab);
  for (let i = 0; i < 8; i++) shared[i] = i + 1;
  const own = new Uint8Array([10, 20, 30]);
  const w = new Worker(__filename, { workerData: "" });
  let got = 0;
  // one message at a time: the next goes out only after the reply to the last (the printout reads the shared
  // memory, so a worker already on the next message would make the output depend on timing)
  w.on("message", (m: string) => {
    got++;
    console.log(m, shared.join(","), own.join(","));
    if (got === 1) w.postMessage(shared.subarray(4)); // a view on part of it: offset kept
    else if (got === 2) { w.postMessage(own); own[0] = 99; } // a copy: the worker's writes stay there
    else w.terminate();
  });
  w.postMessage(shared);          // the same memory: the worker's writes show here
} else {
  parentPort.on("message", (m: Uint8Array) => {
    const before = m.join(",");
    for (let i = 0; i < m.length; i++) m[i] = m[i] * 2;
    parentPort.postMessage(`worker got ${before} (offset ${m.byteOffset}, ${m.length} bytes) and doubled`);
  });
}
