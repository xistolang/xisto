// process.exit(): the 'exit' listeners run with the code (not 'beforeExit'), and nothing after the call
process.on("beforeExit", () => console.log("beforeExit: never"));
process.on("exit", (code: number) => console.log("exit with", code, process.exitCode));
setTimeout(() => console.log("timer: never"), 50);
console.log("before");
process.exitCode = 0;
process.exit(0);
console.log("after: never");
