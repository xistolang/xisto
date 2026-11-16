// expect: g.throw(e) on a generator is not supported
function* g(): Generator<number> {
  yield 1;
}
const it = g();
it.throw(new Error("x"));
