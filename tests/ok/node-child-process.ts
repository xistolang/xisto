// node:child_process: spawn with streamed output, exec/execFile callbacks, spawnSync/execFileSync/execSync, a missing
// command, a failing one, stdin, kill (same output as Node)
import { spawn, exec, execFile, spawnSync, execFileSync, execSync } from "node:child_process";

console.log(execSync("echo sync", { encoding: "utf8" }).trim(), execFileSync("printf", ["%s-%s", "a", "b"], { encoding: "utf8" }));
const s = spawnSync("sh", ["-c", "echo out; echo err 1>&2; exit 3"], { encoding: "utf8" });
console.log(s.status, JSON.stringify(s.stdout), JSON.stringify(s.stderr));
const piped = spawnSync("tr", ["a-z", "A-Z"], { input: "shout", encoding: "utf8" });
console.log(piped.stdout);
try {
  execFileSync("sh", ["-c", "echo boom 1>&2; exit 2"], { encoding: "utf8", stdio: "pipe" });
} catch (err) {
  console.log("thrown:", (err as Error).message.split("\n")[0]);
}

const ls = spawn("sh", ["-c", "echo one; sleep 0.05; echo two"]);
ls.stdout.setEncoding("utf8");
ls.stdout.on("data", (d: string) => console.log("stdout:", JSON.stringify(d)));
ls.on("exit", (code: number | null) => console.log("exit", code));
ls.on("close", (code: number | null) => {
  console.log("close", code);
  exec("echo hello && echo warn 1>&2", (err, stdout, stderr) => {
    console.log("exec:", err === null, JSON.stringify(stdout), JSON.stringify(stderr));
    exec("exit 4", (err2) => {
      console.log("exec failed:", err2 !== null, err2 !== null ? err2.code : 0);
      execFile("echo", ["file", "args"], (err3, out3) => {
        console.log("execFile:", err3 === null, out3.trim());
        const missing = spawn("definitely-not-a-command-xyz");
        missing.on("error", (e: Error) => console.log("error:", e.message));
        const cat = spawn("cat");
        cat.stdout.setEncoding("utf8");
        cat.stdout.on("data", (d: string) => console.log("cat says", JSON.stringify(d)));
        cat.on("close", (c: number | null) => console.log("cat closed", c));
        cat.stdin.write("from stdin");
        cat.stdin.end();
        const sleeper = spawn("sleep", ["5"]);
        sleeper.on("exit", (c: number | null, sig: string | null) => console.log("sleeper", c, sig));
        setTimeout(() => sleeper.kill(), 30);
      });
    });
  });
});
