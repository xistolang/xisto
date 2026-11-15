// process.on('unhandledRejection'): a rejection with no handler by the end of the microtasks goes to the listener
// with (reason, promise) and the program goes on. Without that listener it goes to 'uncaughtException' with the
// origin "unhandledRejection" (Node's default mode). setUncaughtExceptionCaptureCallback takes the errors instead.
function onUnhandled(reason: Error, p: unknown): void {
  console.log("unhandled:", reason.message, typeof p);
}
process.on("unhandledRejection", onUnhandled);
process.on("uncaughtException", (err: Error, origin: string) => {
  console.log("uncaught:", err.message, origin);
});

Promise.reject(new Error("first"));
const handled = Promise.reject(new Error("handled"));
handled.catch((e: Error) => console.log("catch:", e.message));

const later = new Promise<number>((_resolve, reject) => {
  setTimeout(() => reject(new Error("later")), 5);
});

setTimeout(() => {
  process.off("unhandledRejection", onUnhandled);
  console.log("listeners:", process.listenerCount("unhandledRejection"));
  Promise.reject(new Error("no listener"));
}, 20);

setTimeout(() => {
  process.setUncaughtExceptionCaptureCallback((err: Error) => {
    console.log("captured:", err.message);
  });
  console.log("has capture:", process.hasUncaughtExceptionCaptureCallback());
  setTimeout(() => {
    throw new Error("to the capture");
  }, 1);
}, 40);

setTimeout(() => {
  process.setUncaughtExceptionCaptureCallback(null);
  console.log("end");
}, 60);
console.log("sync end");
