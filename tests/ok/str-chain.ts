// s.replace(...).replace(...): each intermediate string is released as soon as the next one exists; the
// answers are the same, including when a step matches nothing (the same string comes back) and when the
// receiver is not fresh (a variable: not released)
function build(n: number): string {
  let s = "";
  for (let i = 0; i < n; i++) s += i % 3 === 0 ? "abcAB" : i % 3 === 1 ? "xyz" : "aNDcaN";
  return s;
}
const base = build(40);
const a = base.replace(/a[NB]/g, "<2>").replace(/zz/g, "!").replace(/<[^>]*>/g, "|").replaceAll("|", "-");
console.log(a.length, a.slice(0, 30));
const b = base.toUpperCase().replace("ABC", "1").replace("XYZ", "2").replaceAll("AND", "3");
console.log(b.length, b.slice(0, 30));
const c = (base + "tail").replace(/nothing/g, "x").replace(/tail$/, "end");
console.log(c.length, c.slice(c.length - 10));
let d = base;
for (let i = 0; i < 3; i++) d = d.replace(/xyz/, "Q").replace(/Q/, "q" + String(i));
console.log(d.slice(0, 40), base.length);
const parts = [base.replace(/a/g, "A").replace(/A/g, "a"), base.replace(/b/g, "")];
console.log(parts[0] === base, parts[1].length);
