// expect: did you mean 'bump'?
class C {
  n: int = 0;
  bump(): void {
    this.n++;
  }
}
const c: C = new C();
c.bmp();
