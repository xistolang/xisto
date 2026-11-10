// expect: runtime error: uncaught Error (in promise): nobody waits
// a rejection no handler takes ends the program, as in Node (exit code 1)
async function fails(): Promise<number> {
  throw new Error("nobody waits");
}
const p = fails();
const q = p;
console.log("kept");
