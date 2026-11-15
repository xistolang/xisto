// rl.question(query, cb) with piped input: the prompt goes to the output, the next line is the answer; each answer
// asks the next question, then close()
import * as readline from "node:readline";

const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
rl.on("close", () => console.log("\nbye"));
rl.question("What is your name? ", (name: string) => {
  console.log(`Hello, ${name}!`);
  rl.setPrompt("color> ");
  console.log("prompt is", JSON.stringify(rl.getPrompt()));
  rl.question("Favourite color? ", (color: string) => {
    console.log(`${name} likes ${color}`);
    console.log("prompt back to", JSON.stringify(rl.getPrompt()));
    rl.close();
    rl.close(); // (a second close does nothing)
    console.log("closed:", rl.closed);
  });
});
