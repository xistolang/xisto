// typed arrays' at, flat(depth), Array.from of a generator, the docs' examples
const u = new Uint8Array([10, 20, 30]);
const f = new Float64Array([1.5, 2.5]);
const f32 = new Float32Array([0.5]);
const i32 = new Int32Array([3, -1, 2]);
console.log(u.at(-1), u.at(0), u.at(3) ?? "none", f.at(-1), f32.at(0), i32.at(-2), i32.at(-4) ?? "none");
console.log(Buffer.from("hi").at(-1));
const deep: number[][][] = [[[1, 2], [3]], [[4]]];
console.log(deep.flat(), deep.flat(2), deep.flat(Infinity), deep.flat(0), deep.flat(1));
function* fib(): Generator<number> {
  let a = 0, b = 1;
  while (true) {
    yield a;
    [a, b] = [b, a + b];
  }
}
console.log(fib().filter((x) => x % 2 === 0).take(5).toArray());
function* upTo(n: number): Generator<number> {
  for (let i = 1; i <= n; i++) yield i;
}
console.log(Array.from(upTo(4)), Array.from(upTo(3), (x) => x * x));
const { promise, resolve } = Promise.withResolvers<string>();
setTimeout(() => resolve("later"), 5);
promise.then((v) => console.log(v));
