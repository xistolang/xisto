// nick?: string | null: an explicit null is kept apart from absent (JSON.stringify, console.log, in), as on Node;
// never set, or set to undefined, it is left out
class U {
  name: string = "a";
  nick?: string | null;
  age?: number | null;
  ok?: boolean | null;
}
const u = new U();
console.log(JSON.stringify(u));
u.nick = null;
console.log(JSON.stringify(u), "nick" in u);
u.age = null;
u.ok = null;
console.log(JSON.stringify(u), u);
u.nick = undefined;
console.log(JSON.stringify(u));
u.nick = "x";
u.age = 3;
u.ok = false;
console.log(JSON.stringify(u), u);
const v = JSON.parse('{"name":"b","nick":null}') as U;
console.log(JSON.stringify(v), v.nick ?? "empty");
const w = JSON.parse('{"name":"c"}') as U;
console.log(JSON.stringify(w));
const a: U = Object.assign({}, v);
console.log(JSON.stringify(a));
const c = structuredClone(v);
console.log(JSON.stringify(c));
interface I { id: number; tag?: string | null }
const i1: I = { id: 1, tag: null };
const i2: I = { id: 2 };
console.log(JSON.stringify([i1, i2]), i1, i2);
function mk(t: string | null): I { return { id: 3, tag: t }; }
console.log(JSON.stringify(mk(null)), JSON.stringify(mk("q")));
class D { v?: string | null = null; w?: string | null = "w"; }
console.log(JSON.stringify(new D()), new D());
class E extends U { more?: number | null; }
const e = new E();
e.more = null;
e.nick = null;
console.log(JSON.stringify(e));
class W {
  nick?: string | null;
  setNull(): void { this.nick = null; }
  clear(): void { this.nick = undefined; }
}
const w1 = new W();
w1.setNull();
console.log(JSON.stringify(w1));
w1.clear();
console.log(JSON.stringify(w1));
const ws: W[] = [new W(), new W()];
ws[0].nick = null;
console.log(JSON.stringify(ws));
function setIt(o: W): void { o.nick = null; }
const w2 = new W();
setIt(w2);
console.log(JSON.stringify(w2));
interface J { name: string; nick?: string | null }
const j1: J = { name: "ana", nick: null };
const j2: J = { name: "bia" };
console.log(Object.keys(j1), Object.keys(j2), "nick" in j1, "nick" in j2, Object.entries(j1));
class K { name: string = "a"; nick?: string | null; }
const k = new K();
k.nick = null;
console.log(Object.keys(k), Object.values(k));
