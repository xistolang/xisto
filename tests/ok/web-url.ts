// URL, URLSearchParams and the URI functions as Node's globals (WHATWG URL)
function show(u: URL): void {
  console.log([u.href, u.protocol, u.username, u.password, u.host, u.hostname, u.port, u.pathname, u.search, u.hash, u.origin].join(" | "));
}
const inputs: string[] = [
  "https://Example.COM:443/a/./b/../c?x=1&y=two words#frag",
  "http://user:pa ss@host:8080/p%20q/r?",
  "http://h.com",
  "HTTP://h.com:80/",
  "https://h.com/a b/ç?q=é ç'#h é",
  "file:///tmp/x.txt",
  "data:text/plain,hello world",
  "mailto:someone@example.com",
  "ws://h.com:81/socket",
  "http://[::1]:3000/x",
];
for (const s of inputs) show(new URL(s));
const base = "https://cdn.example.com/css/site/main.css?v=3#top";
for (const r of ["../fonts/a.ttf", "img.png", "/root.css", "//other.net/x", "?q=1", "#sec", "", "./x/../y", "a/b/..", "https://abs.org/z"]) {
  console.log(r, "->", new URL(r, base).href);
}
try {
  new URL("not a url");
} catch (e) {
  console.log((e as Error).name, (e as Error).message.split(":")[0]);
}
try {
  new URL("http://h.com:99999/");
} catch (e) {
  console.log((e as Error).name);
}
console.log(URL.canParse("x", "http://a.com"), URL.canParse("nope"));
const u = new URL("https://h.com/s?a=1&b=2&a=3");
console.log(u.searchParams.get("a"), u.searchParams.getAll("a").join(","), u.searchParams.has("b"), u.searchParams.get("z"));
u.searchParams.append("c", "x y&z");
u.searchParams.delete("b");
console.log(u.href, u.search);
u.searchParams.set("a", "9");
console.log(u.href, u.searchParams.size);
u.search = "?k=v w";
console.log(u.href, u.searchParams.get("k"));
u.pathname = "/new path/./x";
u.hash = "end";
console.log(u.href, u.toString(), JSON.stringify({ u: u.href }));
const p = new URLSearchParams({ grant_type: "client_credentials", client_id: "abc", scope: "api://x/.default", note: "ç é+*~" });
console.log(p.toString());
const q = new URLSearchParams("?a=1&b=x+y&c=%41%zz&d&=e&a=2");
console.log(q.toString(), q.get("b"), q.get("c"), q.get("d"), q.get(""), q.getAll("a").length);
const e = new URLSearchParams();
e.set("search", "maria");
e.set("first", String(0));
e.set("max", String(20));
e.sort();
console.log(e.toString());
for (const [k, v] of q) console.log(k, "=", v);
q.forEach((v: string, k: string): void => {
  console.log("each", k, v);
});
console.log(Array.from(q.keys()).join(","), Array.from(q.values()).join(","));
console.log(encodeURIComponent("a b&c=d/é?#ç~*'()!"), encodeURI("http://h.com/a b/é?x=1&y=ç#f"));
console.log(decodeURIComponent("a%20b%26%C3%A9%E2%82%AC%F0%9F%98%80"), decodeURI("a%20b%26c%2F%3F%C3%A9"));
