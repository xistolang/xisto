// expect: cannot assign to 'id': it is readonly
class User {
  readonly id: int;
  constructor(id: int) {
    this.id = id;
  }
}
const u = new User(1);
u.id = 2;
