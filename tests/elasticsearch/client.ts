// @elastic/elasticsearch (packages/@elastic/elasticsearch) against tests/elasticsearch/server.mjs: each call's
// method, path, query string and body (what the fake cluster echoes back), the decoded responses, a 404 as an
// error and with ignore: [404], a retry after a 503, a timeout, basic auth and NDJSON bulk bodies.
import { Client, ResponseError, TimeoutError } from "@elastic/elasticsearch";

interface Echo { method: string; path: string; body: string; auth: string; type: string }
interface Source { company: string; amount: number }
interface Hit { _index: string; _id: string; _source: Source }
interface Total { value: int; relation: string }
interface Hits { total: Total; hits: Hit[] }
interface SearchResult { took: int; hits: Hits; echo: Echo }
interface WriteResult { _id: string; result: string; echo: Echo }
interface GetResult { _id: string; found: boolean; _source: Source; echo: Echo }
interface Missing { _id: string; found: boolean }
interface BulkItem { _index: string; _id: string; status: int }
interface BulkLine { index: BulkItem }
interface BulkResult { errors: boolean; lines: int; ends_with_newline: boolean; echo: Echo }
interface Health { status: string; echo: Echo }
interface Ack { acknowledged: boolean; echo: Echo }
interface Count { count: int; echo: Echo }
interface Version { number: string }
interface Info { name: string; version: Version; echo: Echo }

const show = (what: string, e: Echo): void => {
  console.log(`${what}: ${e.method} ${e.path}${e.body !== "" ? " " + e.body.replaceAll("\n", "\\n") : ""}${e.type !== "" ? " [" + e.type + "]" : ""}`);
};

const port = process.argv[2];
const client = new Client({ node: `http://127.0.0.1:${port}/`, auth: { username: "elastic", password: "s3cret" }, requestTimeout: 2000, maxRetries: 1 });

const info: Info = await client.info();
console.log("info", info.name, info.version.number, "auth", info.echo.auth);
console.log("ping", await client.ping());

const health: Health = await client.cluster.health({ wait_for_status: "yellow", timeout: "5s" }, { requestTimeout: 1000, maxRetries: 0 });
show("health " + health.status, health.echo);

const found: SearchResult = await client.search({ index: "orders", size: 10, from: 0, track_total_hits: true, routing: "r1", query: { term: { status: 2 } } });
show("search", found.echo);
console.log("hits", found.hits.total.value, found.hits.hits.length, found.hits.hits[0]._source.company, found.hits.hits[1]._source.amount);

const counted: Count = await client.count({ index: "orders,orders-2" });
show("count " + String(counted.count), counted.echo);

const put: WriteResult = await client.index({ index: "orders", id: "1", refresh: "wait_for", document: { company: "Ação & Cia", amount: 10.5, tags: ["a", "b"] } });
show("index " + put.result, put.echo);
const auto: WriteResult = await client.index({ index: "orders", document: { company: "C" } });
show("index " + auto._id, auto.echo);

const got: GetResult = await client.get({ index: "orders", id: "1", _source: true });
show("get " + got._source.company, got.echo);
try {
  await client.get({ index: "orders", id: "missing" });
  console.log("get missing: no error?");
} catch (e) {
  if (e instanceof ResponseError) console.log("get missing: error", e.name, e.meta.statusCode, e.statusCode);
  else console.log("get missing: error");
}
const missing: Missing = await client.get({ index: "orders", id: "missing" }, { ignore: [404] });
console.log("get missing with ignore:", missing._id, missing.found);

const upd: WriteResult = await client.update({ index: "orders", id: "1", retry_on_conflict: 3, doc: { amount: 11 }, doc_as_upsert: true });
show("update " + upd.result, upd.echo);
const del: WriteResult = await client.delete({ index: "orders", id: "1", refresh: true });
show("delete " + del.result, del.echo);

const ops: object[] = [];
for (let i: int = 0; i < 3; i++) {
  ops.push({ index: { _index: "orders", _id: `k${i}` } });
  ops.push({ company: `Company ${i}`, amount: Number(i) * 1.5 });
}
const bulk: BulkResult = await client.bulk({ operations: ops, refresh: false });
show("bulk errors=" + String(bulk.errors) + " lines=" + String(bulk.lines) + " newline=" + String(bulk.ends_with_newline), bulk.echo);

const created: Ack = await client.indices.create({ index: "batch", mappings: { properties: { amount: { type: "double" } } } });
show("indices.create " + String(created.acknowledged), created.echo);
console.log("indices.exists", await client.indices.exists({ index: "orders" }), await client.indices.exists({ index: "other" }));
const mapped: Ack = await client.indices.putMapping({ index: "batch", properties: { note: { type: "keyword" } } });
show("indices.putMapping", mapped.echo);
const tpl: Ack = await client.indices.putIndexTemplate({ name: "orders-tpl", index_patterns: ["orders-*"], priority: 10 });
show("indices.putIndexTemplate", tpl.echo);
const ilm: Ack = await client.ilm.putLifecycle({ name: "keep-30d", policy: { phases: { delete: { min_age: "30d" } } } });
show("ilm.putLifecycle", ilm.echo);

try {
  await client.search({ index: "nope", size: 1 });
} catch (e) {
  console.log("error:", e.message.replaceAll("\n", "\\n").replaceAll("\t", "\\t"));
}

const retried: Count = await client.count({ index: "flaky" });
show("retry after 503: count " + String(retried.count), retried.echo);

try {
  await client.search({ index: "slow", size: 1 }, { requestTimeout: 200, maxRetries: 0 });
  console.log("slow: no timeout?");
} catch (e) {
  console.log("slow:", e.message, e.name, e instanceof TimeoutError);
}

await client.close();
try {
  await client.info();
} catch (e) {
  console.log("after close:", e.message);
}
