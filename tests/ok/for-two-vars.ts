// for with two (or more) variables and a comma-separated update; each iteration has its own copies (closures)
const s: number[] = [1, 2, 3, 4, 5];
for (let i = 0, j = s.length - 1; i < j; i++, j--) {
  const t = s[i];
  s[i] = s[j];
  s[j] = t;
}
console.log(s.join(","));
let out = "";
for (let i = 0, j = 10; i < j; i += 2, j--) {
  if (i === 4) continue;
  out += `${i}:${j} `;
}
console.log(out);
const fs: (() => number)[] = [];
for (let i = 0, k = 1; i < 3; i++, k *= 2) fs.push(() => i * 10 + k);
console.log(fs.map((f) => f()).join(","));
let a = 0;
for (let x = 1, y = 2, z = 3; x < 4; x++, y++, z++) a += x * y * z;
console.log(a);
