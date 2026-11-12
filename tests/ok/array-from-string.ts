// Array.from(string): its characters (code points), as JavaScript gives them
const t = "Ação 😀x";
const cs = Array.from(t);
console.log(cs.length, cs.join("|"), Array.from("").length);
