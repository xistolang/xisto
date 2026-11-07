// expect: must be Dog, but this is Animal
class Animal {
  n: int = 1;
}
class Dog extends Animal {}
function walk(d: Dog): int {
  return d.n;
}
console.log(walk(new Animal()));
