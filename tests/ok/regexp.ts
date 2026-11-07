// RegExp: literals and new RegExp, test/exec with lastIndex, match/matchAll, replace with $n/$<name>/callbacks,
// split with groups and limits, search, flags, named groups, Unicode positions (code points).
const email = /^[\w.+-]+@[\w-]+\.[\w.]+$/;
console.log(email.test("ana@site.com"), email.test("not an email"), email.source, email.flags);

const date = /(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})/;
const m = date.exec("today is 2026-11-06, ok");
if (m !== null) {
  console.log(m[0], m[1], m[2], m[3], m.index, m.length);
  console.log(m.groups!.year, m.groups!.month, m.groups!.day);
}
console.log(date.exec("no date here") === null);

// global: exec walks with lastIndex
const word = /\w+/g;
let w = word.exec("one two three");
const seen: string[] = [];
while (w !== null) {
  seen.push(`${w[0]}@${w.index}`);
  w = word.exec("one two three");
}
console.log(seen.join(" "), word.lastIndex);

// match
const all = "a1b22c333".match(/\d+/g);
console.log(all !== null ? all.length : 0, all !== null ? all[0] + "," + all[1] + "," + all[2] : "");
const first = "a1b22c333".match(/(\d)(\d)/);
console.log(first !== null ? `${first[0]} ${first[1]} ${first[2]} ${first.index}` : "none");
console.log("abc".match(/x/) === null);

// matchAll
const pairs: string[] = [];
for (const p of "k1=v1; k2=v2; k3=v3".matchAll(/(\w+)=(\w+)/g)) {
  pairs.push(`${p[1]}:${p[2]}@${p.index}`);
}
console.log(pairs.join(" "));

// replace
console.log("2026-11-06".replace(/(\d+)-(\d+)-(\d+)/, "$3/$2/$1"));
console.log("2026-11-06".replace(date, "$<day>.$<month>.$<year>"));
console.log("a-b-c".replace(/-/g, "+"), "a-b-c".replace(/-/, "+"), "a-b-c".replaceAll(/-/g, "_"));
console.log("x".replace(/x/, "[$&] [$$] [$`] [$']"));
console.log("hello world".replace(/o/g, (s: string): string => s.toUpperCase()));
console.log("john smith".replace(/(\w+) (\w+)/, (all: string, a: string, b: string): string => `${b.toUpperCase()}, ${a}`));
console.log("aaa".replace(/a*?/g, "-"), "abc".replace(/(?:)/g, "."));

// split
console.log("a, b,c ,  d".split(/\s*,\s*/).join("|"));
console.log("a1b2c3".split(/(\d)/).join("|"));
console.log("a1b2c3".split(/\d/, 2).join("|"));
console.log("abc".split(/(?:)/).join("|"), "".split(/x/).length, "".split(/(?:)/).length);

// search, flags, case
console.log("hello World".search(/world/i), "hello".search(/z/));
const multi = "line one\nline two\nthird";
const starts = multi.match(/^\w+/gm);
console.log(starts !== null ? starts.length : 0);
console.log(/a.c/s.test("a\nc"), /a.c/.test("a\nc"));
const re2 = new RegExp("b+", "g");
console.log("abbbcbb".replace(re2, "B"), re2.global, re2.flags, re2.toString());

// Unicode: positions are code points
const u = "ção é ótimo";
const um = /ó\w+/.exec(u);
console.log(um !== null ? `${um[0]} ${um.index}` : "none", "ção é".search(/é/));
console.log("añb".split(/ñ/).join("|"), "日本語のテキスト".replace(/の/, "-"));

// errors are SyntaxErrors at run time for new RegExp
try {
  new RegExp("(unclosed");
} catch (e) {
  console.log((e as Error).name);
}

// whole-pattern runs (\s+ \w+ [^a]+), with characters beyond ASCII in and out of the class
console.log("x  y z  w".split(/\s+/).join("|"), "olá mundo".match(/\w+/g)!.join("|"), "béçaxa".split(/[^a]+/).length);
const runs = "  aa 12 bb  ".matchAll(/\S+/g);
const where: string[] = [];
for (const r of runs) {
  where.push(`${r[0]}@${r.index}`);
}
console.log(where.join(" "), "a1b22c333".replace(/\d+/g, "#"), "é1é22".search(/\d+/));
