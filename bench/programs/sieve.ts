// Arrays, loops inteiros e memória.
function sieve(limit: int): int {
  const composite: boolean[] = new Array<boolean>(limit + 1).fill(false);
  let count: int = 0;
  for (let i: int = 2; i <= limit; i++) {
    if (!composite[i]) {
      count++;
      for (let j: int = i * i; j <= limit; j += i) {
        composite[j] = true;
      }
    }
  }
  return count;
}

const limit: int = 30000000;
console.log("primes up to", limit, sieve(limit));
