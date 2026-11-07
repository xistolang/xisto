// switch (int, string, fallthrough, default), break/continue in loops, ternary.
function kind(n: int): string {
  switch (n) {
    case 0:
      return "zero";
    case 1:
    case 2:
      return "small";
    default:
      return "big";
  }
}

function weekday(d: string): int {
  let n: int = 0;
  switch (d) {
    case "sat":
    case "sun":
      n = 0;
      break;
    case "mon":
      n = 1;
      // fallthrough
    case "tue":
      n += 10;
      break;
    default:
      n = -1;
  }
  return n;
}

let total: int = 0;
for (let i: int = 0; i < 10; i++) {
  if (i % 2 === 0) {
    continue;
  }
  if (i > 7) {
    break;
  }
  const label: string = "n" + String(i);
  total += i;
  console.log(label);
}
let k: int = 0;
while (true) {
  k++;
  if (k === 3) {
    break;
  }
}
console.log(total, k, kind(0), kind(2), kind(99), weekday("sun"), weekday("mon"), weekday("tue"), weekday("x"));
const big: boolean = total > 5;
const s: string = big ? "big" : "small";
const v: number = big ? 1.5 : 2;
const w: int = total > 100 ? 1 : 2;
console.log(s, v, w, (k === 3 ? "three" : "other") + "!", big ? [1, 2].length : 0);
