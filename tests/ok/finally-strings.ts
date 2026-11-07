// try/finally and the extra string methods.
const log: string[] = [];
function risky(n: int): int {
  if (n > 2) {
    throw new Error(`too big: ${n}`);
  }
  return n * 10;
}
function attempt(n: int): string {
  let result: string = "";
  try {
    result = String(risky(n));
  } catch (e) {
    result = "caught " + e.message;
  } finally {
    log.push(`finally ${n}`);
  }
  return result;
}
console.log(attempt(1), attempt(5), log);
try {
  try {
    risky(9);
  } finally {
    log.push("inner finally");
  }
} catch (e) {
  console.log("outer got", e.message);
}
try {
  try {
    risky(7);
  } catch (e) {
    throw new Error("rethrown: " + e.message);
  } finally {
    log.push("cleanup");
  }
} catch (e) {
  console.log(e.message, log.length, log[log.length - 1]);
}
let ok: boolean = false;
try {
  ok = true;
} finally {
  log.push("no error");
}
console.log(ok, log.join(" | "));
const csv: string = "a,b,,c";
console.log(csv.split(","), csv.split(",").length, "abc".split(""), "x".split(","), "a-b-c".split("-").map((p) => p.toUpperCase()));
console.log("aaa".replace("a", "b"), "aaa".replaceAll("a", "bb"), "path/to/file".replaceAll("/", "\\\\"), "nada".replace("x", "y"));
console.log("7".padStart(3, "0"), "7".padEnd(3), "ab".padStart(7, "xyz"), "abc".padStart(2), "ação".padStart(6, "*"));
console.log("ab".repeat(3), "".repeat(5), "a/b/c".lastIndexOf("/"), "abc".lastIndexOf("z"), "  x  ".trimStart() + "|", "|" + "  x  ".trimEnd());
console.log("hello".charAt(1), "hello".charAt(9), "ção".charAt(0));
