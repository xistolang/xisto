// replaceAll that does not grow and toUpperCase/toLowerCase of ASCII rewrite a temporary in its own buffer when
// nothing else holds it; when something does, or the text is not ASCII, they make a new string as before

function long(unit: string, times: int): string {
  let s = "";
  for (let i = 0; i < times; i++) s += unit;
  return s;
}

function digest(s: string): string {
  let h = 0;
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) % 1000000007;
  return `${s.length}:${h}:${s.slice(0, 12)}|${s.slice(s.length - 12)}`;
}

const text = long("acgt\nACGT\nnnxx\n", 5000);

// the k-nucleotide chain: slice, remove the newlines, upper case
console.log(digest(text.slice(5, text.length - 3).replaceAll("\n", "").toUpperCase()));
console.log(digest(text.slice(1).replaceAll("\n", "").toLowerCase()));
// a longer pattern, a shorter replacement
console.log(digest(text.slice(2).replaceAll("ACGT", "x")));
console.log(digest(text.slice(2).replaceAll("nn", "")));
// nothing matches: the same text
console.log(digest(text.slice(3).replaceAll("Q", "")));
// the source still in use afterwards: it must not change
const kept = text.slice(0, 40000);
const upper = kept.toUpperCase();
console.log(digest(kept), digest(upper));
const removed = kept.replaceAll("\n", "");
console.log(digest(kept), digest(removed));
// a chain whose first link is a variable: the variable keeps its value
const v = long("ab\n", 3000);
console.log(digest(v.replaceAll("\n", "").toUpperCase()), digest(v));
// not ASCII: the case mapping makes a new string (ß becomes SS), lengths in code units
const accented = long("straße ção\n", 2000);
const up = accented.slice(0).replaceAll("\n", "").toUpperCase();
console.log(up.length, digest(up));
console.log(accented.slice(1).replaceAll("ç", "c").length);
// the result of an in-place step used twice
const twice = text.slice(4).replaceAll("\n", "");
console.log(digest(twice.toUpperCase()), digest(twice));
