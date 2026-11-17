// The legacy url API: url.parse (and its DEP0169 warning, once), url.format, url.resolve, Url.resolveObject
import * as url from "node:url";
import { parse, format, resolve, Url } from "node:url";

const inputs = [
  "http://user:pw@Host.COM:8080/p/a/t/h?query=string#hash",
  "https://example.com",
  "HTTP://www.Example.com/",
  "//some/path?x=1",
  "/just/a/path?with=query&a=b#frag",
  "/plain",
  "relative/path",
  "mailto:someone@example.com",
  "javascript:alert('hi there')",
  "file:///etc/passwd",
  "file://localhost/tmp/x",
  "  http://trim.me/x  ",
  "http:\\\\evil.com\\foo\\bar?x\\y",
  "http://a@b@c/",
  "http://a@b?@c",
  "http://[::1]:3000/ipv6",
  "http://x.com/path with spaces/<>\"'`{}|^",
  "http://host:/empty-port",
  "ftp://ftp.example.org/file.txt",
  "xmpp:isaacschlueter@jabber.org",
  "?only=query",
  "#only-hash",
  "",
  "http://ex.com/a?b=c#d?e#f",
  "HTTP://USER:PASS@EXAMPLE.COM:443/",
  "http://user%20name:p%40ss@host.com/",
  "svn+ssh://foo/bar",
  "dash-test://foo/bar",
  "http://a.com/\ttab\nnl",
  "news:comp.lang.javascript",
  "http://x/%zz",
];
for (const s of inputs) console.log(JSON.stringify(s), parse(s));

const q = parse("http://a.com/p?a=1&b=2&a=3&c", true);
console.log(q.search, JSON.stringify(q.query), q.query.a, q.query.c);
const q2 = parse("/p?x=%20y", true);
console.log(q2.pathname, JSON.stringify(q2.query));
const q3 = url.parse("http://a.com/nothing", true);
console.log(q3.search, JSON.stringify(q3.query));
const h = parse("//host/path", false, true);
console.log(h.host, h.pathname, h.slashes);
for (const bad of ["http://host:abc/bad-port", "http://a:1:2/"]) {
  try {
    parse(bad);
  } catch (e) {
    console.log((e as Error).name, (e as Error).message, (e as any).code);
  }
}
try {
  parse("http://a[b]/");
} catch (e) {
  console.log((e as Error).name, (e as Error).message, (e as any).code);
}

console.log(format({ protocol: "https", hostname: "example.com", pathname: "/some/path", query: { page: 1, format: "json" } }));
console.log(format({ protocol: "http:", host: "a.com:81", pathname: "x", search: "q=1", hash: "h" }));
console.log(format({ protocol: "http", hostname: "::1", port: 8080, pathname: "/v6" }));
console.log(format({ protocol: "mailto", auth: "me", hostname: "x.org" }));
console.log(format({ protocol: "file", pathname: "/tmp/f" }));
console.log(format({ protocol: "http", slashes: true, auth: "a b:c@d", host: "h", pathname: "/p#x?y", search: "?s#t" }));
console.log(format({ pathname: "/only" }), JSON.stringify(format({})));
console.log(format("HTTP://Example.COM/a b"));
const w = new URL("https://u:p@example.com:8443/a/b?c=1#frag");
console.log(format(w), format(w, { fragment: false, search: false, auth: false }), format(w, { auth: false }));

const rel = [
  ["/foo/bar/baz", "quux"], ["/foo/bar/baz", "quux/asdf"], ["/foo/bar/baz", "quux/baz"], ["/foo/bar/baz", "../quux/baz"],
  ["/foo/bar/baz", "/bar"], ["/foo/bar/baz/", "quux"], ["/foo/bar/baz/", "quux/baz"], ["/foo/bar/baz", "../../../../../../../../quux/baz"],
  ["/foo/bar/baz", "../../../../../../../quux/baz"], ["/foo", "."], ["/foo", ".."], ["/foo/", "."], ["/foo/", ".."], ["/foo/bar", "."],
  ["/foo/bar", ".."], ["/foo/bar/", "."], ["/foo/bar/", ".."], ["foo/bar", "../../../baz"], ["foo/bar/", "../../../baz"],
  ["http://example.com/b//c//d;p?q#blarg", "https:#hash2"], ["http://example.com/b//c//d;p?q#blarg", "https:/p/a/t/h?s#hash2"],
  ["http://example.com/b//c//d;p?q#blarg", "https://u:p@h.com/p/a/t/h?s#hash2"], ["http://example.com/b//c//d;p?q#blarg", "https:/a/b/c/d"],
  ["http://example.com/b//c//d;p?q#blarg", "http:#hash2"], ["http://example.com/b//c//d;p?q#blarg", "http:/p/a/t/h?s#hash2"],
  ["http://example.com/b//c//d;p?q#blarg", "http://u:p@h.com/p/a/t/h?s#hash2"], ["http://example.com/b//c//d;p?q#blarg", "http:/a/b/c/d"],
  ["/foo/bar/baz", "/../etc/passwd"], ["http://localhost", "file:///Users/foo"], ["http://localhost", "file://foo/Users"],
  ["https://registry.npmjs.org", "@foo/bar"], ["http://a/b/c/d;p?q", "g:h"], ["http://a/b/c/d;p?q", "g"], ["http://a/b/c/d;p?q", "./g"],
  ["http://a/b/c/d;p?q", "g/"], ["http://a/b/c/d;p?q", "/g"], ["http://a/b/c/d;p?q", "//g"], ["http://a/b/c/d;p?q", "?y"],
  ["http://a/b/c/d;p?q", "g?y"], ["http://a/b/c/d;p?q", "#s"], ["http://a/b/c/d;p?q", "g#s"], ["http://a/b/c/d;p?q", ""],
  ["http://a/b/c/d;p?q", "../../../g"], ["http://a/b/c/d;p?q", "g;x=1/../y"], ["http://a/b/c/d;p?q", "http:g"],
  ["mailto:local1@domain1", "local2@domain2"], ["mailto:local1@domain1", "?subject=hi"], ["mailto:a@b", "mailto:c@d"],
  ["http://user:pass@a.com/", "http://b.com/x"], ["http://a.com/x", "//b.com/y"], ["http://a.com", "x"], ["http://a.com/?q", ""],
  ["foo", "bar"], ["", "x/y"], ["http://example.com/a/b/", "./c/../d/."],
];
for (const r of rel) console.log(JSON.stringify(r[0]), JSON.stringify(r[1]), JSON.stringify(resolve(r[0], r[1])));

const base = parse("http://a.com/x/y?z#w");
const ro = base.resolveObject("../q?r");
console.log(ro.href, ro.pathname, ro.search, ro.hash, ro instanceof Url);
console.log(base.resolve("/abs"), url.resolve("http://h/", "//other/p"));
