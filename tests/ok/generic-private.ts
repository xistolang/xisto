// a private member of another instance of the same generic class (lean<L>() of Query<R>): TypeScript's one class
class Box<T> {
  private v: T[] = [];
  private tag: string = '';
  add(x: T): Box<T> { this.v.push(x); return this; }
  retag<U>(f: (x: T) => U): Box<U> {
    const b = new Box<U>();
    b.v = this.v.map(f);
    b.tag = this.tag + '+';
    return b;
  }
  show(): string { return this.tag + this.v.join(','); }
}
console.log(new Box<number>().add(1).add(2).retag((n) => 'n' + n).show());
