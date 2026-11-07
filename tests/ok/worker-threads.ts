// node:worker_threads (runtime/xisto_net.h): the program again on threads, text messages both ways, typed JSON
// in them, a pool fed one task at a time, terminate, isMainThread / threadId / workerData per thread
import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, threadId, workerData } from "node:worker_threads";

class Job {
  id: int = 0;
  n: int = 0;
}

function sumTo(n: int): int {
  let s = 0;
  for (let i = 1; i <= n; i++) s += i;
  return s;
}

if (isMainThread) {
  console.log("main", threadId, workerData === "", availableParallelism() > 0);
  // one worker per job, the answer as text
  const answers: string[] = ["", "", ""];
  let left = 3;
  for (let k = 0; k < 3; k++) {
    const j = new Job();
    j.id = k;
    j.n = (k + 1) * 10;
    new Worker(__filename, { workerData: JSON.stringify(j) }).on("message", (m: string) => {
      answers[k] = m;
      left--;
      if (left === 0) {
        console.log(answers.join(" | "));
        // a pool of two, fed one job at a time, then told to stop
        const results: int[] = new Array<int>(6).fill(0);
        let next = 0, pending = 6;
        for (let w = 0; w < 2; w++) {
          const wk = new Worker(__filename, { workerData: "pool" });
          wk.on("message", (r: string) => {
            const done: Job = JSON.parse(r);
            results[done.id] = done.n;
            pending--;
            if (next < 6) {
              const nj = new Job();
              nj.id = next;
              nj.n = next * 100;
              wk.postMessage(JSON.stringify(nj));
              next++;
            } else wk.terminate();
            if (pending === 0) {
              console.log("pool", results.join(","));
              bytesRound();
            }
          });
          const first = new Job();
          first.id = next;
          first.n = next * 100;
          wk.postMessage(JSON.stringify(first));
          next++;
        }
      }
    });
  }
} else if (workerData === "pool") {
  parentPort.on("message", (m: string) => {
    const j: Job = JSON.parse(m);
    j.n = sumTo(j.n);
    parentPort.postMessage(JSON.stringify(j));
  });
} else if (workerData !== "bytes") {
  const j: Job = JSON.parse(workerData);
  parentPort.postMessage(`job ${j.id}: ${sumTo(j.n)} (worker ${threadId > 0})`);
}
// bytes both ways: a Uint8Array message and a listener that declares it (after the pool, for a fixed order)
function bytesRound(): void {
  const bw = new Worker(__filename, { workerData: "bytes" });
  bw.on("message", (b: Uint8Array) => {
    console.log("bytes back", b.length, b[0], b[b.length - 1]);
    bw.terminate();
  });
  const out = new Uint8Array(5);
  for (let i = 0; i < 5; i++) out[i] = i * 50;
  bw.postMessage(out);
}
if (!isMainThread && workerData === "bytes") {
  parentPort.on("message", (b: Uint8Array) => {
    const r = new Uint8Array(b.length * 2);
    for (let i = 0; i < r.length; i++) r[i] = b[i % b.length] + 1;
    parentPort.postMessage(r);
  });
}
