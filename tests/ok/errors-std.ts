// try/catch/throw with Error; runtime errors are catchable; std: Date.now, Math.random, process, node:fs.
import { readFileSync, writeFileSync, existsSync, appendFileSync, unlinkSync } from "node:fs";

function divide(a: int, b: int): int {
  if (b === 0) {
    throw new Error("division by zero");
  }
  return Math.trunc(a / b);
}

function safeDivide(a: int, b: int): int {
  try {
    return divide(a, b);
  } catch (e) {
    console.log("caught:", e.message, e.name);
    return -1;
  }
}

console.log(safeDivide(10, 2), safeDivide(1, 0));
class Cfg {
  port: int = 0;
}
try {
  const cfg: Cfg = JSON.parse("{not json");
  console.log(cfg.port);
} catch (e) {
  console.log("runtime error caught:", e.message.length > 0);
}
try {
  try {
    throw new Error("inner");
  } catch (e) {
    throw new Error("outer from " + e.message);
  }
} catch (e) {
  console.log(e.message);
}
const items: string[] = ["a", "b"];
try {
  items.forEach((s) => {
    if (s === "b") {
      throw new Error(`bad item ${s}`);
    }
    console.log("ok", s);
  });
} catch (e) {
  console.log(e.message);
}

const t0: number = Date.now();
const r: number = Math.random();
console.log(t0 > 1700000000000, r >= 0 && r < 1, process.argv.length >= 2);

const path: string = "/tmp/xisto-test-file.txt";
writeFileSync(path, "linha 1\n");
appendFileSync(path, "linha 2\n");
console.log(existsSync(path), readFileSync(path, "utf8"), readFileSync(path).length);
unlinkSync(path);
console.log(existsSync(path));
try {
  readFileSync("/tmp/does-not-exist-xisto.txt", "utf8");
} catch (e) {
  console.log(e.message.startsWith("ENOENT"));
}
