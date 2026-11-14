// Node's timers: setTimeout/setInterval with extra arguments, setImmediate, queueMicrotask, unref/hasRef, and the
// order of microtasks, immediates and timers (same output as Node)
console.log("start");
setTimeout(() => console.log("timeout 10"), 10);
setImmediate(() => console.log("immediate 1"));
Promise.resolve().then(() => console.log("micro 1"));
queueMicrotask(() => console.log("micro 2"));
setImmediate((a: string, b: number) => console.log("immediate args", a, b), "x", 2);
setTimeout((who: string) => console.log("hello", who), 20, "ana");
const t = setTimeout(() => console.log("never"), 50).unref();
console.log(t > 0, setTimeout(() => console.log("t3"), 30).hasRef());
const iv = setInterval(() => {
  console.log("tick");
}, 5);
setTimeout(() => clearInterval(iv), 18);
const imm = setImmediate(() => console.log("cleared imm"));
clearImmediate(imm);
setTimeout(() => {
  setImmediate(() => console.log("imm in timer"));
  setTimeout(() => console.log("timeout0 in timer"), 0);
  Promise.resolve().then(() => console.log("micro in timer"));
}, 25);
console.log("end");
setTimeout(() => {
  process.nextTick(() => console.log("tick in timer"));
  Promise.resolve().then(() => console.log("micro after tick"));
  queueMicrotask(() => console.log("queueMicrotask in timer"));
}, 40);
const r = setTimeout(() => console.log("refreshed timer"), 45);
setTimeout(() => {
  r.refresh();
  console.log("refresh called");
}, 42);
