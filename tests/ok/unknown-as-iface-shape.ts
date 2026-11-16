// unknown as I, I an interface with methods, the object of a class that fits I without implements (no decorator):
// wrapped in the class's adapter at run time (XB-234)
interface I { name: string; hello(): string; }
class K { name: string = "k"; hello(): string { return "hi " + this.name; } }
class L { name: string = "l"; extra = 1; hello(): string { return "hey " + this.name; } }
const items: unknown[] = [new K(), new L()];
for (const u of items) {
  const i = u as I;
  console.log(i.name, i.hello());
}
