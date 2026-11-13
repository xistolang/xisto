// s += t in place (xs_str_append_to): a string held only by its variable grows in its own block; a copy taken
// before (a const, an array element, a field) keeps its value, and s.length stays right after every append
let buf = "";
let lens = 0;
for (let i = 0; i < 5000; i++) {
  buf += (i % 7).toFixed(1) + " ";
  lens += buf.length;
  if (buf.length >= 4096) buf = "";
}
console.log("buffer", buf.length, lens, buf.slice(0, 24));

let s = "start:".repeat(50);
const snaps: string[] = [];
for (let i = 0; i < 6; i++) {
  s += `é${i}`;
  snaps.push(s);
  const before = s;
  s += "x";
  console.log(i, before.length, s.length, before.endsWith("x"), s.endsWith("x"));
}
console.log(snaps.map((t) => t.length).join(","), snaps[2].slice(-8));
s += s;
console.log("doubled", s.length, s.slice(298, 312));

class Log {
  text = "";
  add(t: string): void { this.text += t; }
}
const log = new Log();
for (let i = 0; i < 300; i++) log.add(`${i},`);
const kept = log.text;
log.add("end");
console.log(kept.length, log.text.length, log.text.slice(-10));

const parts: string[] = ["a".repeat(300)];
for (let i = 0; i < 3; i++) parts[0] += "b";
console.log(parts[0].length, parts[0].slice(-4));

let t = "q".repeat(400);
function more(): string { t += "!"; return "?"; }
t += more(); // the right side writes t: t is read first, as in JavaScript
console.log(t.length, t.slice(-3));
