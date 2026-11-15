// Shared by the Rust servers: the runtime (one thread, or WORKERS threads) and the response helpers.
use axum::http::{header, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::Router;

pub const TEXT: &str = "text/plain; charset=utf-8";
pub const JSON: &str = "application/json";

pub fn reply(status: StatusCode, content_type: &'static str, body: impl Into<axum::body::Body>) -> Response {
    (status, [(header::CONTENT_TYPE, content_type)], body.into()).into_response()
}

pub async fn not_found() -> Response {
    reply(StatusCode::NOT_FOUND, "text/plain", "not found")
}

pub async fn method_not_allowed() -> Response {
    reply(StatusCode::METHOD_NOT_ALLOWED, "text/plain", "method not allowed")
}

/// Serves `app` on 127.0.0.1:8080. WORKERS unset or 1: tokio's current_thread runtime (one thread, one event
/// loop, as the other single-process servers); WORKERS=N: the multi-threaded runtime with N worker threads.
pub fn serve(name: &str, app: Router) {
    let workers: usize = std::env::var("WORKERS").ok().and_then(|v| v.parse().ok()).unwrap_or(1);
    let rt = if workers <= 1 {
        tokio::runtime::Builder::new_current_thread().enable_all().build()
    } else {
        tokio::runtime::Builder::new_multi_thread().worker_threads(workers).enable_all().build()
    }
    .expect("tokio runtime");
    rt.block_on(async move {
        let listener = tokio::net::TcpListener::bind("0.0.0.0:8080").await.expect("bind 8080");
        eprintln!("{name}: listening on http://127.0.0.1:8080 ({workers} thread(s))");
        axum::serve(listener, app).await.expect("serve");
    });
}

/// The length JavaScript reports for a string: UTF-16 code units.
pub fn js_length(s: &str) -> usize {
    s.encode_utf16().count()
}
