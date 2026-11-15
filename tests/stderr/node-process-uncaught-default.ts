// no 'uncaughtException' listener: an error from a timer is printed (xisto's format) and the program ends with 1,
// after the 'exit' listeners, as in Node; the next timer never runs
process.on("exit", (code: number) => console.log("exit", code));
setTimeout(() => {
  throw new Error("boom");
}, 1);
setTimeout(() => console.log("never"), 50);
console.log("start");
