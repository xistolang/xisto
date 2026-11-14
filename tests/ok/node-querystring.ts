// node:querystring: parse, stringify, escape and unescape (same output as Node)
import querystring, { parse, stringify, escape, unescape, decode, encode, unescapeBuffer } from "node:querystring";
import * as qs from "querystring";

function show(q: string, sep: string | null = null, eq: string | null = null): void {
  console.log(JSON.stringify(q), "->", JSON.stringify(parse(q, sep, eq)));
}

// ---- parse ----
show("name=Ana&age=31");
show("a=1&a=2&a=3&b=x");
show("q=hello+world&lang=pt-BR");
show("city=S%C3%A3o%20Paulo&emoji=%F0%9F%98%80");
show("flag&empty=&=novalue&&trailing=1&");
show("bad=%zz&half=%4&pct=100%&lone=%");
show("utf=%C3&mixed=%E2%82%ACok&invalid=%FF%FE");
show("key%20with%20space=v%2Bplus");
show("");
show("&&&");
show("a:1;b:2;b:3", ";", ":");
show("a==1&b=2=3");
show("a=1--b=2--c", "--", "=");
show("x=>1&&y=>2", "&&", "=>");
show("ação=sim&key=é%zz");
console.log(JSON.stringify(parse("a=1&b=2&c=3&d=4", null, null, { maxKeys: 2 })));
console.log(JSON.stringify(parse("a=1&b=2&c=3", null, null, { maxKeys: 0 })));
console.log(JSON.stringify(parse("a=%41&b=%42", null, null, { decodeURIComponent: (s: string): string => s.toLowerCase() })));
console.log(JSON.stringify(decode("x=1&y=2")));

const q = parse("user=ana&role=admin&role=ops&page=2");
const user: string = q["user"];
const roles: string[] = q["role"];
console.log(user, roles.length, roles.join("|"), Number(q["page"]) + 1, "missing" in q);

// ---- stringify ----
interface Search {
  term: string;
  page: number;
  exact: boolean;
  tags: string[];
}
const search: Search = { term: "café & bar", page: 2, exact: false, tags: ["a b", "c/d"] };
console.log(stringify(search));
console.log(stringify(search, ";", ":"));
console.log(stringify({ n: Infinity, z: -0, f: 1.5, big: 12345678901234567890 }));
console.log(stringify({ empty: "", list: [] as string[], nums: [1, 2] }));
console.log(stringify({ w: "x y" }, null, null, { encodeURIComponent: (s: string): string => s.replace(" ", "_") }));
console.log(encode({ a: "1" }), JSON.stringify(stringify({})));
const rec: Record<string, string> = { "key with space": "v&w", "ünï": "çödé" };
console.log(stringify(rec));
class Params {
  id: int = 7;
  name: string = "x=y";
}
console.log(stringify(new Params()));

// ---- escape / unescape ----
console.log(escape("a b&c=d/e?f#g"), escape("ação ~!*()'"), escape("😀"));
console.log(unescape("a%20b%26c"), unescape("a+b"), unescape("a+b", true), unescape("%E2%82%AC"));
console.log(JSON.stringify(unescape("%zz%41%")), JSON.stringify(unescape("%C3%28")), JSON.stringify(unescape("é%zz")));
console.log(unescapeBuffer("a%20b%FF").length, unescapeBuffer("%41%42").toString());

// ---- the module object, and namespace import ----
console.log(querystring.stringify({ a: 1 }), JSON.stringify(querystring.parse("k=v")), querystring.escape("x y"), querystring.unescape("x%20y"));
console.log(qs.stringify({ ns: true }), JSON.stringify(qs.parse("ns=true")));

// round trip
const round = parse(stringify({ s: "olá mundo", list: ["1", "2"], sym: "&=?" }));
console.log(JSON.stringify(round));
