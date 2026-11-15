// node:readline/promises: rl.question() gives a promise of the answer; readline.promises from node:readline too; a
// question pending when the interface closes is rejected (AbortError)
import * as readlinePromises from "node:readline/promises";
import * as readline from "node:readline";
import { Readable } from "node:stream";

const rl = readlinePromises.createInterface({ input: process.stdin, output: process.stdout });
const answer = await rl.question("Your answer? ");
console.log(`got [${answer}]`);
rl.close();

const empty = Readable.from(["only line\n"]);
const rl2 = readline.promises.createInterface({ input: empty });
const first = await rl2.question("");
console.log(`first [${first}]`);
try {
  await rl2.question("never answered? ");
} catch (e) {
  console.log("rejected:", (e as Error).name, (e as Error).message);
}
console.log("end");
