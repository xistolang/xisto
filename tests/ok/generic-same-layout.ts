// Repository<A> where Repository<B> goes (no field depends on the type parameter): the same object, by as and by the
// run-time check of an unknown, a function field kept
// build: --tsc-checked
class Repo<T> {
  name: string = '';
  hook: (s: string) => string = (s) => s + '!';
  find(): string { return 'find ' + this.name + ' ' + this.hook('x'); }
}
class A { a = 1; }
class B { b = 2; }
const ra = new Repo<A>();
ra.name = 'ra';
const rb = ra as unknown as Repo<B>;
console.log(rb.find(), rb === (ra as unknown));
const u: unknown = ra;
const rb2 = u as Repo<B>;
console.log(rb2.find());
function take(r: Repo<B>): string { return r.find(); }
console.log(take(ra as unknown as Repo<B>));
