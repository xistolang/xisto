// expect: setting the length of 'ys' to 4 makes it longer (its length is 2 here): xisto arrays do not grow by their length
const LEN = 4;
function make(): number[] {
  const ys = [1, 2];
  ys.length = LEN;
  return ys;
}
console.log(make());
