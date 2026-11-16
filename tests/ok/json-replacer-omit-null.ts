// JSON.stringify(v, (k, v) => v ?? undefined) and (k, v) => v === null ? undefined : v: the members that are null left
// out of objects (compiled as the project option "omitNull", no call per member); array elements stay null
class Addr { city: string = "Recife"; zip: string | null = null; }
class User {
  id: number = 1;
  name: string | null = null;
  nick?: string | null;
  tags: (string | null)[] = ["a", null];
  addr: Addr | null = new Addr();
  extra: Record<string, number | null> = { a: 1, b: null };
}
const u = new User();
u.nick = null;
console.log(JSON.stringify(u));
console.log(JSON.stringify(u, (k, v) => v ?? undefined));
console.log(JSON.stringify(u, (k, v) => v === null ? undefined : v));
console.log(JSON.stringify(u, (_k, val) => (val !== null ? val : undefined), 2));
console.log(JSON.stringify(u));
const rec: Record<string, string | null> = { x: "1", y: null };
console.log(JSON.stringify(rec, (k, v) => v ?? undefined), JSON.stringify(rec));
const any: any = JSON.parse('{"a":null,"b":[null,1],"c":{"d":null,"e":2}}');
console.log(JSON.stringify(any, (k, v) => v ?? undefined), JSON.stringify(any));
const arr: ({ k: number | null } | null)[] = [null, { k: null }, { k: 1 }];
console.log(JSON.stringify(arr, (k, v) => v ?? undefined), JSON.stringify(arr));
console.log(JSON.stringify(u, (k, v) => (k === "id" ? undefined : v)));
