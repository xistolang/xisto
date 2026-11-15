// expect: help: Dog.size is int, but Named.size is number: a class used as Named without `implements Named` must declare its fields with the same types
// XB-200: an interface field is read and written in the object behind the adapter, so it must be a field of the same type
interface Named { name: string; size: number; hello(): string }
class Dog {
  name = "rex";
  size: int = 2;
  hello(): string { return "woof"; }
}
const n: Named = new Dog();
console.log(n.name, n.size);
