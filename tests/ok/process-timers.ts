// node:child_process execSync and node:timers/promises setTimeout, with Node's signatures.
import { execSync } from "node:child_process";
import { setTimeout } from "node:timers/promises";

const out: string = execSync("printf 'a\\nb\\n'", { encoding: "utf8" });
console.log(JSON.stringify(out), out.trim().split("\n").length);
try {
  execSync("exit 3", { encoding: "utf8" });
} catch (e) {
  console.log(e.message.startsWith("Command failed: exit 3"));
}
const t0: number = Date.now();
await setTimeout(60);
const waited: number = Date.now() - t0;
console.log(waited >= 55 && waited < 1000);
