// expect: runtime error: index out of bounds
// the loop calls a closure that raises its limit: the index keeps its check (emit.c, bce_body_ok)
function run(): void {
  let lim = 2;
  const grow = (): void => {
    lim = 10;
  };
  const f = new Float64Array(4);
  for (let i = 0; i < lim; i++) {
    grow();
    f[i] = 1;
  }
  console.log(f.join());
}
run();
