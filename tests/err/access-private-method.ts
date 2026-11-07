// expect: 'audit' is private to Account
class Account {
  private audit(): int {
    return 1;
  }
}
class Savings extends Account {
  check(): int {
    return this.audit();
  }
}
console.log(new Savings().check());
