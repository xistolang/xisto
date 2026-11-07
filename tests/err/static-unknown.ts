// expect: Counter has no static member 'nope'
class Counter {
  static total: int = 0;
}
console.log(Counter.nope);
