// A & B where B is a class with methods (mongoose's HydratedDocument<T> = T & Document): a class made at compile time
// that extends B and holds A's fields; decoded from JSON here (Node's JSON.parse gives plain objects: .expected)
class Doc {
  _id: string = '';
  save(): string { return 'saved ' + this._id; }
  isNew(): boolean { return this._id === ''; }
}
class Foo { nome: string = ''; n: number = 0; tags: string[] = []; }
interface Bar { cd: number; ds?: string; }
type H<T> = T & Doc;
type FooDoc = Foo & Doc;
function onlyFoo(f: Foo): string { return f.nome + '/' + f.n; }
function baseOnly(d: Doc): string { return d.save(); }
const h = JSON.parse('{"_id":"i1","nome":"x","n":2,"tags":["a"]}') as H<Foo>;
console.log(h.nome, h.n, h._id, h.tags.length, h.save(), h.isNew(), h instanceof Doc);
console.log(onlyFoo(h), baseOnly(h));
const f2: FooDoc = JSON.parse('{"_id":"i2","nome":"y","n":3,"tags":[]}');
console.log(f2.save(), onlyFoo(f2));
const b = JSON.parse('{"_id":"i3","cd":7}') as H<Bar>;
console.log(b.cd, b.ds ?? '-', b.save());
const list: H<Foo>[] = [h, f2];
console.log(list.map((d) => d._id).join(','));
// doc.toObject() of a T & Doc: the document read as data
class Doc2 {
  _id: string = '';
  toObject(): Doc2 { return this; }
}
type H2<T> = T & Doc2;
class Named { nome: string = ''; }
function asRow(r: { _id: string; nome: string }): string { return r._id + ':' + r.nome; }
const d2 = JSON.parse('{"_id":"z","nome":"w"}') as H2<Named>;
console.log(asRow(d2.toObject()));
