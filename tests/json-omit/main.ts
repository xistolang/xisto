// "json": { "omitNull": true } in xisto.json: every JSON.stringify leaves out the members that are null (object
// fields, Record entries, the members of an any); an array's elements stay as they are (null)
class Addr { city: string = "Recife"; zip: string | null = null; }
class User {
  id: number = 1;
  name: string | null = null;
  nick?: string | null;
  tags: (string | null)[] = ["a", null];
  addr: Addr | null = new Addr();
  extra: Record<string, number | null> = { a: 1, b: null };
  meta: unknown = null;
}
const u = new User();
u.nick = null;
console.log(JSON.stringify(u));
console.log(JSON.stringify(u, null, 2));
const rec: Record<string, string | null> = { x: "1", y: null };
console.log(JSON.stringify(rec));
const any: any = JSON.parse('{"a":null,"b":[null,1],"c":{"d":null,"e":2}}');
console.log(JSON.stringify(any));
const arr: ({ k: number | null } | null)[] = [null, { k: null }, { k: 1 }];
console.log(JSON.stringify(arr));
