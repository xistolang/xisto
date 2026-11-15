// a class passed where an interface with methods goes, without implements: an optional field of the interface the
// class leaves out (held by the adapter, null until written), an int field where the interface says number (read as
// the number, written back as the int); Object.keys gives the object's own fields (XB-233)
interface Named { name: string; age?: number; nick?: string; greet(): string; }
class Dog { name: string; constructor(n: string) { this.name = n; } greet(): string { return "woof " + this.name; } }
function show(n: Named): void {
  console.log(n.name, n.greet(), n.age, n.nick);
  console.log(Object.keys(n));
  n.age = 3;
  n.nick = "r";
  n.name = "max";
  console.log(n.age, n.nick, `${n.age}`, n.greet());
}
const d = new Dog("rex");
show(d);
console.log(d.name);
interface Pt { x: number; y: number; label(): string }
class IP { x: int; y: int; extra: string = "e"; constructor(x: int, y: int) { this.x = x; this.y = y; } label(): string { return `${this.x},${this.y}`; } }
function sh(p: Pt): void {
  console.log(p.x + 0.5, p.y / 4, p.label());
  p.x = 10;
  p.y += 5;
  p.y++;
  const old = p.x++;
  console.log(old, p.x, p.y, p.label());
  console.log(Object.keys(p), p);
}
const ip = new IP(1, 2);
sh(ip);
console.log(ip.x, ip.y, ip);
class Exact implements Pt { x = 1.5; y = 2; label(): string { return "exact"; } }
sh(new Exact());
