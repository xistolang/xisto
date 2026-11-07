// x as Sub is checked at run time in xisto (TypeScript would let it through): a wrong downcast is a TypeError.
class Animal {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
}
class Dog extends Animal {
  bark(): string {
    return `${this.name}: woof`;
  }
}
class Cat extends Animal {}
const pets: Animal[] = [new Dog("rex"), new Cat("tom")];
console.log((pets[0] as Dog).bark());
try {
  console.log((pets[1] as Dog).bark());
} catch (e) {
  console.log("caught:", e.name, e.message);
}
