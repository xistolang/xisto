// spawnSync with 1 MiB of input through cat: the input is written while the output is read (no deadlock)
import { spawnSync } from "node:child_process";
const input = "x".repeat(1 << 20);
const r = spawnSync("cat", [], { input: input });
console.log(r.status, String(r.stdout).length);
