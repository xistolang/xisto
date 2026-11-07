// Strings as values: concatenation, comparison, methods, conversions, refcounting.
function greet(name: string): string {
  return "olá, " + name + "!";
}

function shout(s: string): string {
  let out: string = s;
  for (let i: int = 0; i < 3; i++) {
    out = out + "!";
  }
  return out.toUpperCase();
}

const who: string = "mundo";
let msg: string = greet(who);
msg += " ";
msg += greet("Isa");
console.log(msg, msg.length, msg === "x", msg !== "x", "abc" === "abc");
console.log(shout("eita"), "xisto".startsWith("xi"), "xisto".endsWith("to"), "xisto".includes("st"), "xisto".indexOf("t"), "xisto".indexOf("z"));
console.log("hello world".slice(6), "hello world".slice(0, 5), "hello".slice(-3), "  pad  ".trim(), "ABC".toLowerCase());
console.log(String(42), String(2.5), String(true), String(-7) + "x", String(0.1 + 0.2));
let acc: string = "";
for (let i: int = 0; i < 5; i++) {
  acc = acc + String(i);
}
console.log(acc, acc.length);
if (acc.startsWith("01")) {
  const inner: string = acc + "/inner";
  console.log(inner);
}
console.log("açúcar".length, "açúcar".slice(1, 3), "açúcar".indexOf("ç"), "ação".slice(-2), [1, 2].length, "fim");
console.log("abcabc".indexOf("c", 3), "abcabc".indexOf("c", 99), "abc".indexOf("", 2), "açúcar".indexOf("car", 2));
