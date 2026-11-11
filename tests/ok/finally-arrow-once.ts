// an arrow inside a finally block: the block is emitted on the normal and on the throw path
function run(xs: number[] | null, fail: boolean): number {
  let n = 0;
  try {
    if (fail) throw new Error("boom");
    n = 1;
  } catch (e) {
    n = -1;
  } finally {
    if (xs?.length) n += xs.filter((x) => x > 1).length;
  }
  return n;
}
console.log(run([1, 2, 3], false), run([5], true), run(null, false));
