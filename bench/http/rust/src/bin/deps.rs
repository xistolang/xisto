// An API whose handlers wait on a slow dependency (the fake database on 8081), on Rust (axum on tokio).
// Same routes and bodies as bench/http/deps/app.xisto.ts. One shared reqwest client: a keep-alive pool.
use axum::extract::{RawQuery, State};
use axum::http::StatusCode;
use axum::response::Response;
use axum::routing::get;
use axum::Router;
use serde::{Deserialize, Serialize};
use xisto_http_bench::common::{method_not_allowed, not_found, reply, serve, JSON};

#[derive(Serialize, Deserialize)]
struct Row {
    id: i64,
    name: String,
}

#[derive(Serialize, Deserialize)]
struct DbReply {
    rows: Vec<Row>,
    ms: i64,
}

#[derive(Serialize)]
struct Item {
    id: i64,
    title: String,
}

#[derive(Clone)]
struct App {
    client: reqwest::Client,
    items: std::sync::Arc<Vec<Item>>,
}

fn ms_of(query: Option<String>) -> String {
    query
        .as_deref()
        .and_then(|q| form_urlencoded::parse(q.as_bytes()).find(|(k, _)| k == "ms").map(|(_, v)| v.into_owned()))
        .unwrap_or_default()
}

async fn query(client: &reqwest::Client, ms: &str) -> DbReply {
    let r = client.get(format!("http://127.0.0.1:8081/q?ms={ms}")).send().await.expect("db call");
    let text = r.text().await.expect("db body");
    serde_json::from_str(&text).expect("db reply")
}

async fn one(State(app): State<App>, RawQuery(q): RawQuery) -> Response {
    let db = query(&app.client, &ms_of(q)).await;
    reply(StatusCode::OK, JSON, serde_json::to_string(&db).unwrap())
}

async fn three(State(app): State<App>, RawQuery(q): RawQuery) -> Response {
    let ms = ms_of(q);
    let a = query(&app.client, &ms).await;
    let b = query(&app.client, &ms).await;
    let c = query(&app.client, &ms).await;
    let total = a.rows.len() + b.rows.len() + c.rows.len();
    reply(StatusCode::OK, JSON, format!("{{\"total\":{total}}}"))
}

async fn mixed(State(app): State<App>, RawQuery(q): RawQuery) -> Response {
    let r = query(&app.client, &ms_of(q)).await;
    // the 50 items are serialized on every request, as JSON.stringify(items) on the other servers
    reply(StatusCode::OK, JSON, format!("{{\"db\":{},\"items\":{}}}", r.rows.len(), serde_json::to_string(&*app.items).unwrap()))
}

fn main() {
    let client = reqwest::Client::builder()
        .pool_idle_timeout(std::time::Duration::from_secs(60))
        .build()
        .expect("http client");
    let items: Vec<Item> = (1..=50).map(|i| Item { id: i, title: format!("item {i}") }).collect();
    let app = Router::new()
        .route("/one", get(one).fallback(method_not_allowed))
        .route("/three", get(three).fallback(method_not_allowed))
        .route("/mixed", get(mixed).fallback(method_not_allowed))
        .fallback(not_found)
        .with_state(App { client, items: std::sync::Arc::new(items) });
    serve("rust", app);
}
