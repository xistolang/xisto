// ?. ?? ??= over T | null; x?: T absent (printed as undefined, left out of JSON); == without coercion
interface Address { city: string; zip?: string; }
interface User { name: string; nick?: string; age?: number; address: Address | null; tags: string[] | null; }
class Box { value: number; constructor(v: number) { this.value = v; } double(): number { return this.value * 2; } }
let calls = 0;
function side(): number { calls++; return 7; }
function maybeBox(on: boolean): Box | null { return on ? new Box(21) : null; }
function show(u: User | null): void {
  console.log(u?.name);
  console.log(u?.address?.city);
  console.log(u?.address?.city ?? "—");
  console.log(u?.nick ?? "(sem nick)");
  console.log(`nick=${u?.nick} age=${u?.age}`);
  console.log(u?.tags?.[0] ?? "no tag");
  console.log(u?.tags?.length ?? -1);
}
show(null);
show({ name: "ana", address: null, tags: null });
show({ name: "bia", nick: "", age: 0, address: { city: "Recife" }, tags: ["x", "y"] });
const z: number | null = 0;
console.log(z ?? 5);
const e: string | null = "";
console.log(e ?? "x");
const nb: boolean | null = false;
console.log(nb ?? true);
console.log(maybeBox(true)?.double());
console.log(maybeBox(false)?.double());
console.log(maybeBox(false)?.double() ?? side(), calls);
console.log(maybeBox(true)?.double() ?? side(), calls);
let s: string | null = null;
s ??= "first";
s ??= "second";
console.log(s);
let n: number | null = null;
n ??= 3;
console.log(n);
const o: User = { name: "c", address: { city: "Natal", zip: "59000" }, tags: null };
o.nick ??= "cc";
console.log(o.nick, o.address?.zip);
const o2: User = { name: "e", nick: "ee", address: null, tags: ["t"] };
console.log(JSON.stringify(o2), JSON.stringify(o2.address));
console.log(JSON.stringify({ name: "d", address: null, tags: null } as User));
function f(x?: number): void { console.log(x, `x=${x}`, x ?? 10, x == null); }
f(); f(0);
const a = 3, b = 3;
console.log(a == b, a != b, "a" == "a", e == null, s != null, u() == null);
function u(): Box | null { return null; }
