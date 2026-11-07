// expect: runtime error: JSON: 'id' must be an integer
class A {
  id: int = 0;
}
const a: A = JSON.parse("{\"id\": \"x\"}");
console.log(a);
