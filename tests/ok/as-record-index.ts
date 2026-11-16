// (o as Record<string, string>)["k"] read in place, with o an object, a Record<string, unknown> or a field typed object:
// the copy the 'as' makes, indexed (it was XS3003 "JSON.parse needs to know the type it produces")
interface Req { headers: Record<string, string>; body: object }
const req: Req = { headers: { k: "h" }, body: { k: "b" } };
const o: object = { k: "o" };
const r2: Record<string, unknown> = { k: "u" };
console.log((o as Record<string, string>)["k"]);
console.log((req.body as Record<string, string>)["k"]);
console.log((r2 as Record<string, string>)["k"]);
const v = (req.body as Record<string, string>)["k"] ?? "none";
console.log(v, (req as unknown as Record<string, unknown>)["headers"] !== null);
function get(x: object, k: string): string { return (x as Record<string, string>)[k] ?? ""; }
console.log(get(o, "k"));
