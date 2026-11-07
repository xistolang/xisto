// expect: Shape is abstract
abstract class Shape {
  abstract area(): number;
}
const s = new Shape();
