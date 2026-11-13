/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   regex-dna program contributed by Jesse Millikan
   Base on the Ruby version by jose fco. gonzalez
   fixed by Matthew Wilson
   ported to Node.js and sped up by Roman Pletnev
   converted from regex-dna program
   fixed by Josh Goldfoot
   multi thread by Andrey Filatkin
   sequential by Isaac Gouy
   (xisto #2: as #1, with the nine counts in a worker (node:worker_threads) while the main thread makes the
   replacements: the two halves of the work at the same time, as the multi-threaded originals do. The worker
   gets the 50 MB of cleaned input as the same string, not a copy (runtime: xs_str_share). Graviton2, 9 runs:
   0,801 s / 1,26 u / 148,5 MB, against #1 1,211 / 1,24 / 148 and Rust #6 1,287 / 1,58 / 150)
*/

import { readFileSync } from "node:fs";
import { Worker, isMainThread, parentPort } from "node:worker_threads";

const regExps = [
  /agggtaaa|tttaccct/ig,
  /[cgt]gggtaaa|tttaccc[acg]/ig,
  /a[act]ggtaaa|tttacc[agt]t/ig,
  /ag[act]gtaaa|tttac[agt]ct/ig,
  /agg[act]taaa|ttta[agt]cct/ig,
  /aggg[acg]aaa|ttt[cgt]ccct/ig,
  /agggt[cgt]aa|tt[acg]accct/ig,
  /agggta[cgt]a|t[acg]taccct/ig,
  /agggtaa[cgt]|[acg]ttaccct/ig
];

// the nine variants and how many times each occurs, one line each
function counts(data: string): string {
  let out = "";
  for (let j = 0; j < regExps.length; j++) {
    const re = regExps[j];
    const m = data.match(re);
    out += `${re.source} ${m ? m.length : 0}\n`;
  }
  return out;
}

function mainThread(): void {
  let data = readFileSync("/dev/stdin", "utf8");
  const initialLen = data.length;

  data = data.replace(/^>.*\n|\n/mg, "");
  const cleanedLen = data.length;

  const w = new Worker(__filename);
  w.postMessage(data); // the counts there, the replacements here

  const endLen = data
    .replace(/tHa[Nt]/g, "<4>")
    .replace(/aND|caN|Ha[DS]|WaS/g, "<3>")
    .replace(/a[NSt]|BY/g, "<2>")
    .replace(/<[^>]*>/g, "|")
    .replace(/\|[^|][^|]*\|/g, "-")
    .length;

  w.on("message", (m: string) => {
    process.stdout.write(m);
    console.log(`\n${initialLen}\n${cleanedLen}\n${endLen}`);
    w.terminate();
  });
}

function workerThread(): void {
  parentPort.on("message", (data: string) => {
    parentPort.postMessage(counts(data));
  });
}

if (isMainThread) mainThread();
else workerThread();
