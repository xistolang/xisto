// expect: 'yield' inside an expression is not supported
function* g(): Generator<number, void, number> {
  console.log(yield 1);
}
console.log([...g()]);
