class Counter {
  total: int = 0;
  add(n: int): int {
    this.total = this.total + n;
    return this.total;
  }
}

function square(x: int): int {
  const y = x * x;
  return y;
}

function sumSquares(n: int): int {
  let s: int = 0;
  for (let i: int = 1; i <= n; i++) {
    s += square(i);
  }
  return s;
}

const c = new Counter();
console.log("start");
const r = sumSquares(3);
c.add(r);
console.log("result", c.total);
