// fetch from plain code (an async main() called at the top level) while a timer is pending: the timer fires during
// the wait and its AbortController.abort() ends the fetch with an AbortError (XB-225), as in Node
import * as http from "node:http";

// a server that never answers: the connection is made and the request waits for a reply
const server = http.createServer((req, res) => {});

async function main(): Promise<void> {
  server.listen(0);
  const a = server.address();
  const port = a !== null && typeof a === "object" ? a.port : 0;
  const ctrl = new AbortController();
  const t0 = Date.now();
  let ticks = 0;
  const iv = setInterval(() => {
    ticks++;
  }, 50);
  setTimeout(() => {
    console.log("timer fired");
    ctrl.abort();
  }, 300);
  try {
    const r = await fetch(`http://127.0.0.1:${port}/slow`, { signal: ctrl.signal });
    console.log("got", r.status);
  } catch (e) {
    const err = e as Error;
    console.log("caught", err.name, err.message);
  }
  clearInterval(iv);
  const dt = Date.now() - t0;
  console.log("aborted in time:", dt >= 250 && dt < 1500);
  console.log("other timers ran meanwhile:", ticks >= 3);
  server.close();
}
main();
