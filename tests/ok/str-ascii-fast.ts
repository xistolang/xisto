// long ASCII strings: length known after slice, replaceAll and toUpperCase; a removed character, eight bytes at a time
const line = "acgtACGTnnnn".repeat(5) + "\n";
const text = ">ONE x\n" + line.repeat(300) + ">TWO y\n" + line.repeat(200);
const body = text.slice(text.indexOf("\n") + 1, text.indexOf(">TWO"));
console.log(body.length, body.slice(0, 8), body.slice(body.length - 8));
const joined = body.replaceAll("\n", "");
console.log(joined.length, joined.indexOf("\n"), joined.slice(55, 70));
const up = joined.toUpperCase();
console.log(up.length, up.slice(0, 16), up.lastIndexOf("ACGT"), up === joined.toUpperCase());
const low = up.toLowerCase();
console.log(low.slice(0, 12), low.length);
// not ASCII after the first 64 bytes: the slow paths
const mixed = "x".repeat(70) + "ação\nçé\n" + "y".repeat(70);
console.log(mixed.length, mixed.replaceAll("\n", "").length, mixed.toUpperCase().slice(68, 78), mixed.slice(70, 74).length);
const removed = mixed.replaceAll("\n", "");
console.log(removed.slice(70, 76), removed.indexOf("é"), removed.length);
// replaceAll that removes a longer pattern, or makes the text longer
const p2 = ("ab--cd--" + "e".repeat(60)).repeat(40);
console.log(p2.replaceAll("--", "").length, p2.replaceAll("-", "+").length, p2.replaceAll("--", "=").slice(0, 12));
// no match at all
console.log("z".repeat(100).replaceAll("\n", "").length);
