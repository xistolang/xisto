// Regressions from review: return/continue/break leaving a try must pop its handler,
// and locals changed inside a try keep their value after a throw (volatile across setjmp).
function first(xs: int[]): int {
  for (const x of xs) {
    try {
      if (x > 2) {
        return x;
      }
    } catch (e) {
      console.log("never");
    }
  }
  return -1;
}

function countEven(n: int): int {
  let c: int = 0;
  for (let i: int = 0; i < n; i++) {
    try {
      if (i % 2 === 1) {
        continue;
      }
      c++;
      if (i > 1000) {
        break;
      }
    } catch (e) {
      console.log("never");
    }
  }
  return c;
}

console.log(first([1, 2, 3, 4]), countEven(600));
// after the early returns above, a throw must land in this handler, not in a dead frame
try {
  throw new Error("still caught");
} catch (e) {
  console.log(e.message);
}

class Box {
  v: int = 0;
}
function volatileLocals(): string {
  let s: string = "a";
  let n: int = 1;
  const b: Box = { v: 0 };
  try {
    s = s + "b";
    n = n + 41;
    b.v = 7;
    const bad: Box = JSON.parse("not json");
    console.log(bad.v);
  } catch (e) {
    s = s + "c";
  }
  return `${s} ${n} ${b.v}`;
}
console.log(volatileLocals());
