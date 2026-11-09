// expect: runtime error: integer overflow: the result left JavaScript's safe integer range
// counters in a local int[] (XB-089): the increments add without the test and the bound K + c * (B - A) passes
// 2^53 - 1, so the elements are checked after the loop: the error comes before anything reads the array
function count(n: int): int[] {
  const t: int[] = new Array<int>(2).fill(9007199254740990);
  for (let j = 0; j < n; j++) t[j & 1]++;
  return t;
}
console.log(count(8)[0]);
