// xisto:profiler: a CPU-bound program with a known split of the work. heavy() does three times the work of light();
// both run spin(), which the compiler inlines into each (a copy per call site), so the profile must keep the
// inlined frames. Also a method, a closure and a recursive function, each with its own hot loop.
import { start, stop, collect, lastSamples } from "xisto:profiler";
import { writeFileSync } from "xisto:fs";

function spin(n: int, seed: int): int {
  let s = seed;
  for (let i = 0; i < n; i++) s = (s * 48271 + 12345) % 2147483647;
  return s;
}
function heavy(n: int): int {
  return spin(3 * n, 1);
}
function light(n: int): int {
  return spin(n, 2);
}
class Parser {
  depth: int = 0;
  scan(n: int): int {
    let h: int = 7;
    for (let i = 0; i < n; i++) h = (h * 31 + i) % 1000003;
    return h;
  }
}
function fib(n: int): int {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

const out = process.argv.length > 2 ? process.argv[2] : "cpu.pprof";
const ms = process.argv.length > 3 ? Number(process.argv[3]) : 2000;
const cpu0 = process.cpuUsage();
console.log("started:", start(99));
console.log("second start:", start(99)); // (one profiler per process)
const p = new Parser();
const each = (k: int): int => p.scan(k);
let acc = 0;
let heavyMs = 0;
let lightMs = 0;
// phases of 300 ms and 100 ms of each function alone, not calls in turn: a sample every 10 ms of CPU must not beat
// with the period of the loop
for (let round = 0; round < Math.max(1, Math.trunc(ms / 400)); round++) {
  let a = performance.now();
  while (performance.now() - a < 300) acc += heavy(30000) % 7;
  heavyMs += performance.now() - a;
  a = performance.now();
  while (performance.now() - a < 100) acc += light(30000) % 7;
  lightMs += performance.now() - a;
}
const t1 = Date.now();
while (Date.now() - t1 < ms / 4) {
  acc += each(50000) % 7;
  acc += fib(22) % 7;
}
stop();
const cpu1 = process.cpuUsage();
const prof = collect();
// (what the profile must match: the time each function took, and the CPU time of the process meanwhile)
console.error(`measured ${(heavyMs / lightMs).toFixed(3)} ${Math.round((cpu1.user + cpu1.system - cpu0.user - cpu0.system) / 1000)}`);
console.log("samples > 0:", lastSamples() > 0, "bytes > 0:", prof.length > 0, acc >= 0);
writeFileSync(out, prof);
const again = collect(); // (nothing since the last collect: an empty profile)
console.log("empty after collect:", lastSamples() === 0, again.length > 0);
