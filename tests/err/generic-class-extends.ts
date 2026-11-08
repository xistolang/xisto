// expect: a generic class cannot extend another class yet
class Base {
  n: number = 0;
}
class Box<T> extends Base {
  v: T | null = null;
}
console.log(new Box<number>().n);
