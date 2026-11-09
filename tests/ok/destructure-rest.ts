// const [a, b, ...rest] = array: rest is a new array with the elements after the ones named
const [a, b, ...rest] = [1, 2, 3, 4, 5];
console.log(a, b, rest, rest.length);
const [x, ...none] = ["only"];
console.log(x, none, none.length);
const [...all] = [7, 8];
all.push(9);
console.log(all);
const src: string[] = ["p", "q", "r"];
const [, second, ...tail] = src;
console.log(second, tail, src);
function firstAndRest(xs: number[]): string {
  const [head, ...others] = xs;
  return `${head} + ${others.length}`;
}
console.log(firstAndRest([1.5, 2.5, 3.5]));
