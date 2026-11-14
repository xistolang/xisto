// node:process: platform, hrtime, uptime, memoryUsage, cpuUsage, nextTick, exit and beforeExit (same output as Node)
import process from "node:process";
console.log(typeof process.platform, ["darwin", "linux", "win32"].includes(process.platform), typeof process.arch);
const t0 = process.hrtime();
const b0 = process.hrtime.bigint();
let x = 0;
for (let i = 0; i < 100000; i++) x += i;
const d = process.hrtime(t0);
console.log(d.length, d[0] >= 0, d[1] >= 0, process.hrtime.bigint() > b0, x);
console.log(process.uptime() > 0, process.uptime() < 60);
const m = process.memoryUsage();
console.log(Object.keys(m).join(","), m.rss > 0, process.memoryUsage.rss() > 0);
const c = process.cpuUsage();
console.log(Object.keys(c).join(","), c.user >= 0, process.cpuUsage(c).user >= 0);
console.log(process.ppid > 0, process.execPath.length > 0, typeof process.version, process.version.startsWith("v"), typeof process.versions.node);
console.log(process.exitCode ?? 0);
process.on("exit", (code: number) => {
  console.log("exit event", code);
});
process.on("beforeExit", (code: number) => {
  console.log("beforeExit", code);
});
process.nextTick((a: string) => console.log("tick", a), "A");
Promise.resolve().then(() => console.log("micro"));
process.nextTick(() => console.log("tick 2"));
console.log("sync end", process.listenerCount("exit"));
