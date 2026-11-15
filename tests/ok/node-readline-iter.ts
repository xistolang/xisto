// for await over a readline Interface, and over process.stdin itself (setEncoding: strings)
import * as readline from "node:readline";

async function sumLines(): Promise<void> {
  const rl = readline.createInterface({ input: process.stdin });
  let total = 0;
  const words: string[] = [];
  for await (const line of rl) {
    if (line.length === 0) continue;
    const [word, num] = line.split(" ");
    words.push(word);
    total += Number(num);
  }
  console.log(words.join(","), total);
}
await sumLines();
console.log("done");
