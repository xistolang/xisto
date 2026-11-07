// Enums in fields and arrays, on both backends (review: QBE printed string enums as integers;
// the C backend did not compile arrays of enums).
enum Color { Red = "red", Green = "green" }
enum Level { Low, High }
class Car {
  c: Color = Color.Red;
  l: Level = Level.High;
}
interface Box {
  c: Color;
}
const car: Car = new Car();
console.log(car.c, car.l);
car.c = Color.Green;
const b: Box = { c: Color.Green };
console.log(b.c, car.c === b.c);
const cs: Color[] = [Color.Red, Color.Green];
cs.push(Color.Red);
console.log(cs.join(","), cs[1], cs.length);
for (const c of cs) {
  console.log(`color ${c}`);
}
const ls: Level[] = [Level.Low, Level.High, Level.Low];
ls.push(Level.High);
let highs: int = 0;
for (const l of ls) {
  if (l === Level.High) {
    highs++;
  }
}
console.log(ls.length, highs, ls[1]);
