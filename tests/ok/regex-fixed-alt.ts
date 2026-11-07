// fixed-length alternations (the regex fast path that finds a match where it starts): first alternative wins,
// classes, flag i, sticky, lastIndex, the end of the text, replace with $&, split, test, match and matchAll
const s: string = "xxtHaNyy tHat aND caN HaD HaS WaS aN aS at BY abcab ABab";
console.log(s.replace(/tHa[Nt]/g, "<4>"));
console.log(s.replace(/aND|caN|Ha[DS]|WaS/g, "<3>"));
console.log(s.replace(/a[NSt]|BY/g, "<2>"));
console.log(s.replace(/ab|abc/g, "[$&]")); // the first alternative wins even when a later one is longer
console.log(s.replace(/abc|ab/g, "[$&]"));
console.log(s.replace(/AB|ca/gi, "_"));
const m = s.match(/a[bc]|B[Ya]/gi);
console.log(m ? m.length : 0, m ? m.join(",") : "");
console.log(/xyz|abd/.test(s), /tHat|qq/.test(s), /ab$|zz/.test("ab"));
const y = /ab|cd/y;
y.lastIndex = 49;
console.log(y.test(s), y.lastIndex);
y.lastIndex = 50;
console.log(y.test(s), y.lastIndex);
console.log("aab".split(/a|b/));
console.log("tail ab".search(/abc|ab/), "tail abc".search(/abc|ab/), "x a".search(/ab|a/));
const all: string[] = [];
for (const r of "agggtaaa TTTACCCT agggtaab".matchAll(/agggtaaa|tttaccct/ig)) all.push(`${r.index}:${r[0]}`);
console.log(all.join(" "));
console.log("ﬀ ß K k".replace(/k|ß/gi, "#")); // non-ASCII text around an ASCII pattern
// delimited runs (P, every character but E, E): <[^>]*>, \|[^|][^|]*\|, "[^"]*", with a minimum, across lines,
// with non-ASCII inside, unterminated, sticky and lazy
const t: string = "a<b>c<>d<é\nf>g<unterminated";
console.log(t.replace(/<[^>]*>/g, "|"), t.match(/<[^>]*>/g));
console.log("x||y|z|w|ab||".replace(/\|[^|][^|]*\|/g, "-"));
console.log("say \"hi\" and \"\" and \"oops".replace(/"[^"]*"/g, "S"));
  console.log("ab<<x>>".replace(/<<[^>]*>/g, "#"), "<<>".search(/<[^>]{2,}>/), "<ab>".search(/<[^>]{2,}>/));
  console.log("<é>".replace(/<[^>][^>]*>/g, "1"), "<>".replace(/<[^>][^>]*>/g, "1"));
  const st = /<[^>]*>/y;
  st.lastIndex = 1;
  console.log(st.test("a<b>"), st.lastIndex, /<[^>]*?>/.exec("x<y>z>")![0]);
