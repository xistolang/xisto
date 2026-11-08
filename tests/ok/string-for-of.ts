// for...of over a string: one string per code point, as JavaScript (a surrogate pair is one element)
// (lengths are not printed: xisto counts code points, JavaScript UTF-16 units; see docs/linguagem.md)
let n = 0;
for (const ch of "aé😀b") {
  n++;
  console.log(ch);
}
console.log(n);
for (const _ch of "") console.log("never");
let rev = "";
for (const c of "xisto") rev = c + rev;
console.log(rev);
const counts = new Map<string, int>();
for (const c of "banana") counts.set(c, (counts.has(c) ? counts.get(c)! : 0) + 1);
console.log([...counts.keys()].join(","), counts.get("a"));
