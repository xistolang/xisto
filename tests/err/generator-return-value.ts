// expect: a generator returns no value
function* g(): Generator<int> {
  yield 1;
  return 2;
}
