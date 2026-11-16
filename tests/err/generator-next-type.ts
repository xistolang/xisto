// expect: the value of 'yield' is the one next(v) sends, and its type is not written
function* g(): Generator<number> {
  const v = yield 1;
  console.log(v);
}
console.log(g().next());
