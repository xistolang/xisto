// Big strings to a worker and back: sent as themselves, not copied (runtime xs_str_share), with a count both
// threads change; the bytes and every result the same as Node's, and each string freed once by the last holder.
import { Worker, isMainThread, parentPort } from "node:worker_threads";

function sum(s: string): number { // a check of every character, cheap enough for a few MB
  let h = 0;
  for (let i = 0; i < s.length; i += 7) h = (h * 31 + s.charCodeAt(i)) % 1000000007;
  return h;
}

function mainThread(): void {
  let big = "";
  const parts: string[] = [];
  for (let i = 0; i < 20000; i++) parts.push(`line ${i} ${"acgt".repeat(i % 13)}\n`);
  big = parts.join("");
  const slice = big.slice(1000, big.length - 1000);
  const small = "a small one";
  const kept: string[] = [];
  let got = 0;
  const w = new Worker(__filename);
  w.on("message", (m: string) => {
    got++;
    kept.push(m); // held after the worker has gone
    console.log(`back ${got}: ${m.length} ${sum(m)}`);
    if (got === 5) {
      w.terminate();
      console.log(`kept ${kept.length}: ${kept[0] === big} ${kept[1].length} ${sum(kept.join(""))}`);
      console.log(`still mine: ${big.length} ${sum(big)} ${slice.length} ${sum(slice)}`);
    }
  });
  console.log(`sent: ${big.length} ${sum(big)} ${slice.length} ${sum(slice)}`);
  w.postMessage(big);
  w.postMessage(big); // the same string twice
  w.postMessage(slice);
  w.postMessage(small);
  w.postMessage(big.toUpperCase());
}

function workerThread(): void {
  let n = 0;
  let first = "";
  parentPort.on("message", (m: string) => {
    n++;
    if (n === 1) {
      first = m;
      parentPort.postMessage(m); // back as it came
    } else if (n === 2) {
      parentPort.postMessage(`${m === first} ${m.length}`);
    } else if (n === 3) {
      parentPort.postMessage(m.replace(/acgt/g, "TTTT")); // a new big one, made here
    } else if (n === 4) {
      parentPort.postMessage(m + "!");
    } else {
      parentPort.postMessage(m.slice(0, 70000)); // a big piece of one sent here
    }
  });
}

if (isMainThread) mainThread();
else workerThread();
