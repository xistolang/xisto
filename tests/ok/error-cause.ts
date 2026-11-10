// new Error(message, { cause }): err.cause is the value given (null/undefined when not given); a thrown object of a
// class that extends Error is not a cause
const inner = new Error("inner");
const e = new Error("outer", { cause: inner });
console.log(e.message, e.cause === inner, (e.cause as Error).message);
const plain = new Error("plain");
console.log(plain.cause == null);
class CodedError extends Error {
  code: int = 7;
}
function fail(n: int): void {
  if (n > 0) throw new Error(`step ${n} failed`, { cause: "disk full" });
  throw new CodedError("coded");
}
const steps: int[] = [1, 0];
for (const n of steps) {
  try {
    fail(n);
  } catch (x) {
    if (x instanceof CodedError) console.log("coded", x.code);
    else console.log((x as Error).message, "|", (x as Error).cause);
  }
}
const chain = new Error("top", { cause: new Error("middle", { cause: new Error("root") }) });
let c: unknown = chain;
const names: string[] = [];
while (c instanceof Error) {
  names.push((c as Error).message);
  c = (c as Error).cause;
}
console.log(names.join(" <- "));
