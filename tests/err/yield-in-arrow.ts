// expect: 'yield' is used inside a generator
function* g(): Generator<int> {
  const f = (): void => {
    yield 1;
  };
  f();
}
