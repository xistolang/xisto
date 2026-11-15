// REST server on Rust (axum on tokio). Same routes as bench/http/server.xisto.ts.
use axum::body::Bytes;
use axum::extract::RawQuery;
use axum::http::StatusCode;
use axum::response::Response;
use axum::routing::{get, post};
use axum::Router;
use xisto_http_bench::common::{js_length, method_not_allowed, not_found, reply, serve, JSON, TEXT};

async fn ping() -> Response {
    reply(StatusCode::OK, TEXT, "pong")
}

async fn hello(RawQuery(query): RawQuery) -> Response {
    // the first `name` wins, as URLSearchParams.get does
    let name = query
        .as_deref()
        .and_then(|q| form_urlencoded::parse(q.as_bytes()).find(|(k, _)| k == "name").map(|(_, v)| v.into_owned()))
        .unwrap_or_default();
    let len = js_length(&name);
    reply(StatusCode::OK, JSON, format!("{{\"hello\":\"{name}\",\"len\":{len}}}"))
}

async fn echo(body: Bytes) -> Response {
    reply(StatusCode::OK, TEXT, body)
}

fn main() {
    let app = Router::new()
        .route("/ping", get(ping).fallback(method_not_allowed))
        .route("/hello", get(hello).fallback(method_not_allowed))
        .route("/echo", post(echo).fallback(method_not_allowed))
        .fallback(not_found);
    serve("rust", app);
}
