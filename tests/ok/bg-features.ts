// what the Benchmarks Game programs needed: bodies without braces, substring, process.stdout.write (text and bytes)
let s = 0;
for (let i = 0; i < 4; i++) s += i;
if (s > 3) s = s * 2; else s = 0;
let k = 0;
while (k < 3) k++;
const xs: int[] = [1, 2, 3];
for (const x of xs) if (x % 2 === 1) s += Number(x);
if (s === 0) console.log("zero"); else if (s > 10) console.log("big", s, k); else console.log("small", s);
const t = "héllo world";
console.log(t.substring(1, 4), t.substring(4, 1), t.substring(-3, 2), t.substring(6), t.substring(3, 100), "[" + t.substring(20) + "]");
process.stdout.write("no newline, ");
console.log("then console.log");
const b = new Uint8Array(3);
b[0] = 79; b[1] = 75; b[2] = 10;
process.stdout.write(b);
