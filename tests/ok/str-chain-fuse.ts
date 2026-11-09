// s.slice(a, b) followed by replaceAll of one character by "" and/or toUpperCase/toLowerCase: one fused pass on an
// ASCII string, the calls as written on anything else; every order and range form gives what Node gives
function lines(n: int, width: int, seed: int): string {
  const letters = "acgtACGTnN";
  let out = "";
  let x = seed;
  for (let i = 0; i < n; i++) {
    x = (x * 1103 + 7) % 9973;
    out += letters.charAt(x % letters.length);
    if ((i + 1) % width === 0) out += "\n";
  }
  return out;
}

const text = ">ONE\n" + lines(100, 7, 3) + ">TWO\n" + lines(5000, 60, 11) + ">THREE x\n" + lines(70001, 61, 5);
const start = text.indexOf("\n", text.indexOf(">THREE")) + 1;
const a = text.slice(start, text.length).replaceAll("\n", "").toUpperCase();
const b = text.slice(start).toUpperCase().replaceAll("\n", "");
const c = text.slice(3, -9).replaceAll("g", "");
const d = text.slice(-500).toLowerCase();
const e = text.slice(10, 4).replaceAll("\n", "").toUpperCase(); // empty
const f = text.slice(start).toUpperCase().replaceAll("G", ""); // case first, then the upper-case letter dropped
const g = text.slice(start).replaceAll("g", "").toUpperCase(); // only the lower-case g dropped
const h = text.slice(-100000, -50000).replaceAll("x", "").toLowerCase(); // nothing to drop
for (const s of [a, b, c, d, e, f, g, h]) {
  let sum = 0;
  for (let i = 0; i < s.length; i++) sum = (sum * 31 + s.charCodeAt(i)) % 1000000007;
  console.log(s.length, sum, s.slice(0, 20), s.slice(-12));
}
// not ASCII: the calls as written
const u = "ação\nmaçã\nÉcole\n".repeat(3000);
const v = u.slice(2, -3).replaceAll("\n", "").toUpperCase();
const w = u.slice(1).toLowerCase().replaceAll("ç", "");
console.log(v.length, v.slice(0, 30), w.length, w.slice(0, 20));
console.log("ab\ncd".slice(0).replaceAll("\n", "").toUpperCase(), "Xy".slice(1).toLowerCase());
