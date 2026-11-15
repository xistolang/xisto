// process.stdin as a stream: 'data' with Buffer chunks, 'end'; pause()/resume(); piping it to process.stdout (which
// is a Writable, with isTTY false here); a program that only looks at process.stdin is not kept alive by it
let bytes = 0;
let chunks = 0;
console.log("stdout is a tty:", process.stdout.isTTY === true, "stdin:", process.stdin.isTTY === true);
process.stdin.on("data", (chunk: Buffer) => {
  chunks++;
  bytes += chunk.length;
  if (chunks === 1) {
    console.log("first chunk is a Buffer:", Buffer.isBuffer(chunk), "starts with", JSON.stringify(chunk.subarray(0, 5).toString()));
    process.stdin.pause();
    console.log("paused:", process.stdin.isPaused());
    setTimeout(() => {
      console.log("resuming");
      process.stdin.resume();
    }, 10);
  }
});
process.stdin.on("end", () => {
  console.log("end:", bytes, "bytes");
});
