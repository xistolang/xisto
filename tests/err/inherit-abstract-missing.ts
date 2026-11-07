// expect: Square does not implement the abstract method Shape.area
abstract class Shape {
  abstract area(): number;
}
class Square extends Shape {
  side: number = 1;
}
console.log(new Square().side);
