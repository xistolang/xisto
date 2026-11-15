// (t as unknown as { name: string }).name, read and written, with t of a class that has the field: the field of t
// itself (a private one too: TypeScript checks nothing through the cast) (XB-234)
class T { name: string = "a"; n: number = 1; private secret: string = "s"; reveal(): string { return this.secret; } }
const t = new T();
const r = (t as unknown as { name: string }).name;
console.log(r);
(t as unknown as { name: string }).name = "b";
(t as unknown as { n: number }).n += 41;
(t as unknown as { secret: string }).secret = "changed";
console.log(t.name, t.n, t.reveal(), (t as unknown as { secret: string }).secret);
