// REST client on Rust (reqwest on tokio's current_thread runtime). Same work as bench/http/client.ts:
// 20000 sequential iterations of GET /ping + GET /hello?name=isa, one keep-alive client.
struct ApiClient {
    base: String,
    http: reqwest::Client,
    calls: u64,
}

impl ApiClient {
    async fn ping(&mut self) -> String {
        self.calls += 1;
        let r = self.http.get(format!("{}/ping", self.base)).send().await.expect("ping");
        r.text().await.expect("ping body")
    }

    async fn hello(&mut self, name: &str) -> String {
        self.calls += 1;
        let r = self.http.get(format!("{}/hello?name={name}", self.base)).send().await.expect("hello");
        r.text().await.expect("hello body")
    }
}

#[tokio::main(flavor = "current_thread")]
async fn main() {
    let mut client = ApiClient { base: "http://127.0.0.1:8080".to_string(), http: reqwest::Client::new(), calls: 0 };
    let n = 20000;
    let mut ok = 0;
    let mut bytes = 0;
    for _ in 0..n {
        let pong = client.ping().await;
        if pong == "pong" {
            ok += 1;
        }
        let h = client.hello("isa").await;
        bytes += h.encode_utf16().count();
    }
    println!("calls {} ok {} bytes {}", client.calls, ok, bytes);
}
