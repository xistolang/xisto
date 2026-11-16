// The order of process.nextTick callbacks, microtasks (promise reactions, queueMicrotask), setImmediate and timers,
// as Node's: the nextTick queue drains before the microtasks (those a microtask queues wait for the microtasks), each
// immediate and each timer callback is followed by the ticks and microtasks it queued, an immediate queued in a timer
// callback runs before a setTimeout(0) queued there; then/catch/finally reactions take Node's microtask turns
// (finally two more than then, XB-254; a callback that returns a promise two more as well)
function out(s: string): void {
  console.log(s);
}
function chain(tag: string, n: number): void {
  let p: Promise<void> = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => out(`${tag}${i}`));
}
function round(tag: string): void {
  out(`${tag} sync`);
  process.nextTick(() => {
    out(`${tag} tick1`);
    process.nextTick(() => out(`${tag} tick1-tick`));
    Promise.resolve().then(() => out(`${tag} tick1-micro`));
  });
  Promise.resolve()
    .then(() => {
    out(`${tag} micro1`);
    process.nextTick(() => out(`${tag} micro1-tick`));
    queueMicrotask(() => out(`${tag} micro1-qm`));
  })
    .then(() => out(`${tag} micro2`));
  queueMicrotask(() => out(`${tag} qm`));
  process.nextTick(() => out(`${tag} tick2`));
  chain(`${tag} c`, 6);
  Promise.resolve(1)
    .finally(() => out(`${tag} fin`))
    .then(() => out(`${tag} fin-then`));
  Promise.reject(new Error("x"))
    .finally(() => out(`${tag} fin-rej`))
    .catch(() => out(`${tag} fin-rej-catch`));
  Promise.resolve(2)
    .then((): Promise<number> => {
    out(`${tag} ret-promise`);
    return Promise.resolve(3);
  })
    .then(() => out(`${tag} ret-promise-then`));
  Promise.reject(new Error("y"))
    .catch(() => out(`${tag} catch`))
    .then(() => out(`${tag} catch-then`));
  new Promise<number>((res) => res(5)).then(() => out(`${tag} new-then`));
}
round("main");
setTimeout(() => {
  round("timer");
  setTimeout(() => out("timer: setTimeout 0"), 0);
  setImmediate(() => out("timer: setImmediate"));
}, 5);
setTimeout(() => {
  setImmediate(() => {
    out("imm1");
    process.nextTick(() => out("imm1 tick"));
    Promise.resolve().then(() => out("imm1 micro"));
  });
  setImmediate(() => {
    out("imm2");
    setImmediate(() => out("imm2 imm"));
  });
}, 20);
setTimeout(() => {
  out("t-a");
  process.nextTick(() => out("t-a tick"));
  Promise.resolve().then(() => out("t-a micro"));
}, 40);
setTimeout(() => out("t-b"), 40);
