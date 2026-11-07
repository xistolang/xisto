// expect: 'area' overrides Shape.area with another signature
class Shape {
  area(): number {
    return 0;
  }
}
class Square extends Shape {
  area(): string {
    return "x";
  }
}
console.log(new Square().area());
