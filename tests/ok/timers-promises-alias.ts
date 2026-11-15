// XB-206: import { setTimeout as sleep } from "node:timers/promises" names only `sleep`: the global
// setTimeout(cb, ms) next to it stays Node's timer (it was read as the promise one: XS8026)
import { setTimeout as sleep } from "node:timers/promises";
setTimeout(() => console.log("global timer"), 5);
const id = setTimeout(() => console.log("never"), 1);
clearTimeout(id);
async function main(): Promise<void> {
  await sleep(1);
  console.log("slept");
  await sleep(20);
  console.log("slept again");
}
main();
