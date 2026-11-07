// expect: cannot infer type parameter T of 'make'
function make<T>(): T[] {
  return [];
}
const xs = make();
console.log(xs.length);
