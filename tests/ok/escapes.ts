// String escapes as JavaScript has them: \0 (a real NUL inside the string), \xHH, \uHHHH, surrogate pairs,
// \u{...}, \b \f \v, line continuation, identity escapes; in strings and in template literals
const nul = "a\0b";
console.log(nul.length, nul.charCodeAt(1), nul.split("\0"), nul === "a\x00b", nul.indexOf("\u0000"));
console.log("\x41\x42\x43", "caf\u00e9", "\u{1F600}".codePointAt(0), "\uD83D\uDE00" === "\u{1F600}", "\u{41}\u{e9}");
console.log("\b\f\v".length, "\b".charCodeAt(0), "\f".charCodeAt(0), "\v".charCodeAt(0));
console.log("line \
continued", '\'', "\"", "\\", "\$", "\/", "\é", "\u2028".length);
const x = 7;
console.log(`t\x41b\u00e9\0${x}\u{E9}\``.length, `t\x41b\u00E9${x}\`\${x}`);
console.log(JSON.stringify("a\0b\u001fc"), "\x1b[0m".length, " ".length);
const parts: string[] = ["k\0v", "x"];
console.log(parts.join("|").length, parts[0].replace("\0", "="));
// charCodeAt / codePointAt / String.fromCharCode / String.fromCodePoint
const w = "Açaí!";
const codes: number[] = [];
for (let i: int = 0; i < w.length; i++) { codes.push(w.charCodeAt(i)); }
console.log(codes, w.codePointAt(1), w.charCodeAt(99), w.charCodeAt(), codes.map((c: number) => String.fromCharCode(c)).join("") === w);
console.log(String.fromCharCode(72, 105), String.fromCodePoint(0x1F600, 33), String.fromCharCode(0xD83D, 0xDE00), String.fromCharCode(65 + 65536), String.fromCharCode());
let h = 0;
for (let i: int = 0; i < w.length; i++) { h = (h * 31 + w.charCodeAt(i)) % 1000003; }
console.log(h, "z".charCodeAt(0) - "a".charCodeAt(0));
