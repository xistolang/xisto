function classify(n: int): int {
  if (n < 0) {
    return -1;
  } else if (n === 0) {
    return 0;
  } else {
    return 1;
  }
}

function sumTo(n: number): number {
  let s: number = 0;
  let i: number = 0;
  while (i <= n) {
    s += i;
    i++;
  }
  return s;
}

function countDown(n: number): void {
  for (let i: number = n; i > 0; i--) {
    console.log(i);
  }
}

let total: int = 0;
for (let k: int = 0; k < 5; k++) {
  total += classify(k - 2);
}
console.log(total, sumTo(100), classify(-7) === -1, !(true && false) || false);
countDown(3);
{
  const total: number = 99;
  console.log("shadow", total);
}
console.log("outer", total);
