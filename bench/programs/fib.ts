// Recursão e chamadas de função.
function fib(n: int): int {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

const n: int = 35;
console.log("fib", n, fib(n));
