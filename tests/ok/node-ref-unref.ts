// ChildProcess.ref/unref, Worker.ref/unref, MessageChannel and MessagePort close/unref/ref/hasRef/start: what keeps
// the program alive (an unref'd child or worker still running does not)
import { spawn } from "node:child_process";
import { Worker, MessageChannel, isMainThread, parentPort } from "node:worker_threads";

if (isMainThread) {
  const started = Date.now();
  process.on("exit", () => console.log("exit, quick:", Date.now() - started < 2500));

  // a child that runs for 4 s, unref'd: the program does not wait for it
  const slow = spawn("sleep", ["4"]);
  slow.unref();

  // unref then ref again: its output and exit come as usual
  const echo = spawn("sh", ["-c", "echo hi"]);
  echo.unref();
  echo.ref();
  echo.stdout.on("data", (d: Buffer) => process.stdout.write("child: " + d.toString()));
  echo.on("exit", (code: int | null) => {
    console.log("child exit", code);

    // a worker that never ends, unref'd after its first message
    const w = new Worker(__filename);
    w.on("message", (m: string) => {
      console.log("worker says", m);
      w.unref();
    });
    w.ref();

    // a channel in this thread: close() on one port ends both, after what was already sent
    const { port1, port2 } = new MessageChannel();
    port1.start();
    port1.on("message", (m: string) => {
      console.log("p1 got", m);
      if (m === "b") port1.close();
    });
    port2.on("message", (m: string) => console.log("p2 got", m));
    port2.postMessage("a");
    port2.postMessage("b");
    port2.postMessage("c");
    port1.postMessage("x");
    console.log("ports", port1.hasRef(), port2.hasRef());

    // a port listening forever, unref'd: it does not keep the program alive
    const idle = new MessageChannel();
    idle.port1.on("message", (m: string) => console.log("never", m));
    idle.port1.unref();
    console.log("idle", idle.port1.hasRef());
    idle.port1.ref();
    console.log("idle", idle.port1.hasRef());
    idle.port1.unref();
  });
} else {
  parentPort.postMessage("ready");
  setInterval(() => {}, 1000);
}
