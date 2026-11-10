// forms ports ask for (docs/port-recursos.md): do-while, labeled loops, IIFE, String.raw, swap, regexes
let n = 0;
do {
  n++;
  if (n === 2) continue;
  console.log("do", n);
} while (n < 4);
do n += 10; while (n < 30);
console.log(n);

const grid: int[][] = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
let found = -1;
outer: for (const row of grid) {
  for (const v of row) {
    if (v === 5) {
      found = v;
      break outer;
    }
  }
}
console.log("found", found);
let seen = "";
rows: for (let i = 0; i < 3; i++) {
  for (let j = 0; j < 3; j++) {
    if (j > i) continue rows;
    switch (j) {
      case 1:
        if (i === 2) break rows;
        break;
      default:
        break;
    }
    seen += `${i}${j} `;
  }
}
console.log(seen);

const sum = ((a: number, b: number): number => a + b)(2, 3);
const msg = (() => {
  const parts: string[] = ["i", "i", "f", "e"];
  return parts.join("");
})();
console.log(sum, msg);
const adder = (k: number) => (x: number): number => x + k;
console.log(adder(10)(5));

const NUM = String.raw`(\d*[.,]\d+|\d+)`;
const re = new RegExp(String.raw`valor:\s*` + NUM, "i");
console.log(NUM, re.test("VALOR: 12,5"), String.raw`a\nb${n}\t`);

const xs: int[] = [1, 2, 3];
[xs[0], xs[2]] = [xs[2], xs[0]];
console.log(xs);
let a = "a", b = "b";
[a, b] = [b, a];
console.log(a, b);
interface P { x: number; y: string }
const pt: P = { x: 1, y: "y" };
let px = 0, py = "";
({ x: px, y: py } = pt);
console.log(px, py);

const texto = 'say "hi"';
console.log(`"${texto.replace(/"/g, "'")}"`, `${texto.replace(/"/g, '""')}`);
const err = "ORA-12345 failed";
console.log(!(err.length < 3 || /ORA-\d{5}/.test(err)));
