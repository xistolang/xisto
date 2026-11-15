// process.stdin keeps the program alive only while it is read: looked at, or with a 'data' listener and paused,
// it lets the program end when the rest is done (stdin here is a file that would give data)
console.log("isTTY:", process.stdin.isTTY === true);
process.stdin.on("data", (chunk: Buffer) => console.log("data", chunk.length));
process.stdin.pause();
setTimeout(() => console.log("timer, then the end"), 5);
