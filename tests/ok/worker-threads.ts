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
            if (pending === 0) console.log("pool", results.join(","));
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
} else {
  const j: Job = JSON.parse(workerData);
  parentPort.postMessage(`job ${j.id}: ${sumTo(j.n)} (worker ${threadId > 0})`);
}
