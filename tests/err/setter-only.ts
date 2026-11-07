// expect: 'secret' of Vault has a setter but no getter
class Vault {
  private value: string = "";
  set secret(s: string) {
    this.value = s;
  }
}
const v = new Vault();
v.secret = "x";
console.log(v.secret);
