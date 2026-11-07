// expect: 'balance' is private to Account
class Account {
  private balance: int = 0;
}
const a = new Account();
console.log(a.balance);
