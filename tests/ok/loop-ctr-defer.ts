// A while loop's counter whose range test waits for the loop's end (dctr): the counts must be the same as Node's,
// and the loops the rule refuses (the counter read in the loop, a break, a start that is not a literal) as well.

function flipsOf(start: int[]): int {
  const a: int[] = start.slice();
  let flips = 0;
  let k = a[0];
  while (k !== 0) {
    for (let i = 0; 2 * i < k; i++) {
      const t = a[i]; a[i] = a[k - i]; a[k - i] = t;
    }
    k = a[0];
    flips += 1;
  }
  return flips;
}

function collatz(n: int): int {
  let steps = 0;
  let x = n;
  while (x !== 1) {
    x = x % 2 === 0 ? Math.trunc(x / 2) : 3 * x + 1;
    steps++;
  }
  return steps;
}

// refused: the counter is read inside the loop
function readInside(n: int): int {
  let c = 0;
  let x = n;
  let sum = 0;
  while (x > 0) {
    c += 1;
    sum += c;
    x -= 1;
  }
  return sum;
}

// refused: a break
function withBreak(n: int): int {
  let c = 0;
  let x = 0;
  while (x < n) {
    x += 3;
    if (x === 9) break;
    c += 1;
  }
  return c;
}

// refused: the counter does not start from a literal
function fromParam(c0: int, n: int): int {
  let c = c0;
  let x = 0;
  while (x < n) {
    x += 1;
    c += 1;
  }
  return c;
}

console.log(flipsOf([3, 1, 2, 0]), flipsOf([0, 1, 2]), flipsOf([4, 3, 2, 1, 0]), flipsOf([2, 0, 1]));
console.log(collatz(27), collatz(1), collatz(97));
console.log(readInside(5), withBreak(20), withBreak(5), fromParam(10, 4), fromParam(-3, 0));
