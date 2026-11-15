// expect: 'instanceof' on an 'object' is always false: an object holds a copy of the data, not the User object
// An object holds JSON data, not the class instance: instanceof on it would always be false, so it is refused
class User {
  constructor(public name: string) {}
}
const items: object[] = [new User("Isa")];
if (items[0] instanceof User) console.log("user");
