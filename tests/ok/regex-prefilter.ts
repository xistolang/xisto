// the K-byte prefix filter of the regex engine: alternations of fixed strings and classes (K = 8, 4, 3, 2),
// with and without flag i, matches at the very start, overlapping candidates, and right at the end of the text
let seed: int = 7;
function rnd(n: int): int { seed = (seed * 48271) % 2147483647; return seed % n; }
const alpha = "acgtACGT<>|N";
let text = "agggtaaa";
for (let i = 0; i < 4000; i++) text += alpha.charAt(rnd(alpha.length));
text += "tttaccctaggg|BY";
const res = [
  /agggtaaa|tttaccct/ig, /[cgt]gggtaaa|tttaccc[acg]/ig, /a[act]gg|tt[acg]a/ig, /aggg/g, /tHa[Nt]/g,
  /aND|caN|Ha[DS]|WaS/g, /a[NSt]|BY/g, /ag|ta/gi, /<g>|<t>/g, /ACGT|acgt/g, /a(?:cg|tt)t/gi, /(gt|ta)a/g
];
for (const re of res) {
  const m = text.match(re);
  const r = text.replace(re, "#");
  let first = "";
  if (m) for (let k = 0; k < 3 && k < m.length; k++) first += `${m[k]},`;
  console.log(re.source, m ? m.length : 0, first, r.length, r.slice(-12), re.test(text));
}
console.log("aggg".search(/aggg/), "xaggg".search(/aggg/), "agg".search(/aggg/), "zz".replace(/ab|cd/g, "-"));
