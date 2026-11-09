// A fake Elasticsearch for tests/elasticsearch: answers the calls of packages/@elastic/elasticsearch with
// bodies shaped like a real cluster's, and puts what it received (method, path, body, authorization) in "echo".
// Usage: node server.mjs PORT
import http from "node:http";

const port = Number(process.argv[2]);
let flaky = 0;
const reply = (res, status, body) => {
  res.writeHead(status, { "content-type": "application/json", "x-elastic-product": "Elasticsearch" });
  res.end(body === undefined ? "" : JSON.stringify(body));
};
const notFoundIndex = (idx) => ({
  error: {
    root_cause: [{ type: "index_not_found_exception", reason: `no such index [${idx}]`, index: idx }],
    type: "index_not_found_exception", reason: `no such index [${idx}]`, index: idx,
  },
  status: 404,
});

http.createServer((req, res) => {
  let data = "";
  req.on("data", (c) => (data += c));
  req.on("end", () => {
    const url = new URL(req.url, "http://x");
    const echo = { method: req.method, path: url.pathname + url.search, body: data, auth: req.headers.authorization ?? "", type: req.headers["content-type"] ?? "" };
    const p = url.pathname;
    if (p === "/" && req.method === "HEAD") return reply(res, 200);
    if (p === "/") return reply(res, 200, { name: "fake", cluster_name: "test", version: { number: "8.19.1" }, echo });
    if (p === "/_cluster/health") return reply(res, 200, { cluster_name: "test", status: "green", number_of_nodes: 1, echo });
    if (p.startsWith("/slow")) return setTimeout(() => reply(res, 200, { took: 600, echo }), 600);
    if (p.startsWith("/flaky")) {
      flaky++;
      if (flaky % 2 === 1) return reply(res, 503, { error: { type: "unavailable", reason: "try again" }, status: 503 });
      return reply(res, 200, { count: 7, echo });
    }
    if (p.startsWith("/nope")) return reply(res, 404, notFoundIndex("nope"));
    if (p.endsWith("/_search")) {
      return reply(res, 200, {
        took: 3, timed_out: false,
        hits: { total: { value: 2, relation: "eq" }, max_score: 1, hits: [
          { _index: "orders", _id: "1", _score: 1, _source: { company: "Ação & Cia", amount: 10.5 } },
          { _index: "orders", _id: "2", _score: 1, _source: { company: "B", amount: 3 } },
        ] },
        echo,
      });
    }
    if (p.endsWith("/_count")) return reply(res, 200, { count: 2, echo });
    if (p.endsWith("/_bulk")) {
      const lines = data.split("\n").filter((l) => l !== "");
      const items = [];
      for (let i = 0; i < lines.length; i++) {
        const action = JSON.parse(lines[i]);
        const op = Object.keys(action)[0];
        items.push({ [op]: { _index: action[op]._index, _id: action[op]._id ?? `auto${i}`, status: 201, result: "created" } });
        if (op !== "delete") i++;
      }
      return reply(res, 200, { took: 1, errors: false, items, lines: lines.length, ends_with_newline: data.endsWith("\n"), echo });
    }
    const m = p.match(/^\/([^/]+)\/_doc\/([^/]+)$/);
    if (m && req.method === "GET") {
      if (m[2] === "missing") return reply(res, 404, { _index: m[1], _id: m[2], found: false });
      return reply(res, 200, { _index: m[1], _id: m[2], _version: 1, found: true, _source: { company: "Ação & Cia", amount: 10.5 }, echo });
    }
    if (m && req.method === "PUT") return reply(res, 201, { _index: m[1], _id: m[2], _version: 1, result: "created", echo });
    if (m && req.method === "DELETE") return reply(res, 200, { _index: m[1], _id: m[2], result: "deleted", echo });
    if (p.match(/^\/[^/]+\/_doc$/) && req.method === "POST") return reply(res, 201, { _id: "generated", result: "created", echo });
    { const u = p.match(/^\/([^/]+)\/_update\/([^/]+)$/); if (u) return reply(res, 200, { _index: u[1], _id: u[2], result: "updated", echo }); }
    if (p.match(/^\/[^/]+\/_mapping$/)) return reply(res, 200, { acknowledged: true, echo });
    if (p.startsWith("/_index_template/") || p.startsWith("/_ilm/policy/")) return reply(res, 200, { acknowledged: true, echo });
    if (req.method === "HEAD") return reply(res, p === "/orders" ? 200 : 404);
    if (req.method === "PUT") return reply(res, 200, { acknowledged: true, shards_acknowledged: true, index: p.slice(1), echo });
    if (req.method === "DELETE") return reply(res, 200, { acknowledged: true, echo });
    reply(res, 400, { error: { type: "illegal_argument_exception", reason: `no handler for ${req.method} ${p}` }, status: 400 });
  });
}).listen(port, "127.0.0.1", () => console.log("listening"));
