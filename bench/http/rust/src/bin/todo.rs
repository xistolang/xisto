// The todo API on Rust (axum on tokio, serde_json). Same routes, bodies and status codes as
// bench/http/todo/server.xisto.ts:
//   GET /todos, POST /todos, GET /todos/{id}, PUT /todos/{id}, DELETE /todos/{id}
use std::sync::{Arc, RwLock};

use axum::body::Bytes;
use axum::extract::{Path, State};
use axum::http::StatusCode;
use axum::response::Response;
use axum::routing::get;
use axum::Router;
use serde::{Deserialize, Serialize};
use xisto_http_bench::common::{method_not_allowed, not_found, reply, serve, JSON, TEXT};

#[derive(Serialize, Clone)]
struct Todo {
    id: i64,
    title: String,
    done: bool,
}

#[derive(Deserialize)]
struct NewTodo {
    title: String,
}

#[derive(Deserialize)]
struct TodoUpdate {
    title: String,
    done: bool,
}

#[derive(Default)]
struct Store {
    items: Vec<Todo>,
    next_id: i64,
}

type Shared = Arc<RwLock<Store>>;

// The xisto server answers JSON errors through HttpResponse.status, which sends text/plain; its JSON.parse
// failures go out as application/json. Both are kept so the responses match byte for byte.
fn todo_not_found() -> Response {
    reply(StatusCode::NOT_FOUND, TEXT, r#"{"error":"not found"}"#)
}

/// The JSON error the xisto server sends for a body it cannot read, in its words where they map one to one.
fn bad_json(body: &[u8], e: serde_json::Error) -> Response {
    let msg = e.to_string();
    let what = if e.is_eof() {
        format!("unexpected end at offset {}", body.len())
    } else if let Some(rest) = msg.strip_prefix("missing field `") {
        format!("missing field '{}'", rest.split('`').next().unwrap_or(""))
    } else if msg.contains("expected a string") {
        "'title' must be a string".to_string()
    } else if msg.contains("expected a boolean") {
        "'done' must be true or false".to_string()
    } else if msg.contains("expected struct") {
        "'value' must be an object".to_string()
    } else {
        // other syntax errors: serde_json's own words, without its position suffix
        msg.split(" at line ").next().unwrap_or(&msg).to_string()
    };
    let out = serde_json::json!({ "error": format!("JSON: {what}") });
    reply(StatusCode::BAD_REQUEST, JSON, out.to_string())
}

/// Number(s) as JavaScript reads an id: a whole number >= 1, else -1.
fn parse_id(s: &str) -> i64 {
    let t = s.trim();
    let n: f64 = if t.is_empty() { 0.0 } else { t.parse().unwrap_or(f64::NAN) };
    if !n.is_finite() || n.fract() != 0.0 || n < 1.0 {
        return -1;
    }
    n as i64
}

async fn list_todos(State(store): State<Shared>) -> Response {
    let s = store.read().unwrap();
    reply(StatusCode::OK, JSON, serde_json::to_string(&s.items).unwrap())
}

async fn create_todo(State(store): State<Shared>, body: Bytes) -> Response {
    let input: NewTodo = match serde_json::from_slice(&body) {
        Ok(v) => v,
        Err(e) => return bad_json(&body, e),
    };
    let title = input.title.trim();
    if title.is_empty() {
        return reply(StatusCode::BAD_REQUEST, TEXT, r#"{"error":"title is required"}"#);
    }
    let mut s = store.write().unwrap();
    s.next_id += 1;
    let t = Todo { id: s.next_id, title: title.to_string(), done: false };
    let out = serde_json::to_string(&t).unwrap();
    s.items.push(t);
    reply(StatusCode::CREATED, TEXT, out)
}

async fn get_todo(State(store): State<Shared>, Path(id): Path<String>) -> Response {
    let id = parse_id(&id);
    let s = store.read().unwrap();
    match s.items.iter().find(|t| t.id == id) {
        Some(t) => reply(StatusCode::OK, JSON, serde_json::to_string(t).unwrap()),
        None => todo_not_found(),
    }
}

async fn update_todo(State(store): State<Shared>, Path(id): Path<String>, body: Bytes) -> Response {
    let id = parse_id(&id);
    let mut s = store.write().unwrap();
    let Some(t) = s.items.iter_mut().find(|t| t.id == id) else {
        return todo_not_found();
    };
    let input: TodoUpdate = match serde_json::from_slice(&body) {
        Ok(v) => v,
        Err(e) => return bad_json(&body, e),
    };
    if !input.title.is_empty() {
        t.title = input.title;
    }
    t.done = input.done;
    reply(StatusCode::OK, JSON, serde_json::to_string(t).unwrap())
}

async fn delete_todo(State(store): State<Shared>, Path(id): Path<String>) -> Response {
    let id = parse_id(&id);
    let mut s = store.write().unwrap();
    match s.items.iter().position(|t| t.id == id) {
        Some(i) => {
            s.items.remove(i);
            reply(StatusCode::NO_CONTENT, TEXT, "")
        }
        None => todo_not_found(),
    }
}

fn main() {
    let store: Shared = Arc::new(RwLock::new(Store::default()));
    let app = Router::new()
        .route("/todos", get(list_todos).post(create_todo).fallback(method_not_allowed))
        .route("/todos/{id}", get(get_todo).put(update_todo).delete(delete_todo).fallback(method_not_allowed))
        .fallback(not_found)
        .with_state(store);
    serve("rust", app);
}
