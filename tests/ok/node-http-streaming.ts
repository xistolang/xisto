// node:http streams a response: the head goes with the first write, each write is sent at once (chunked), so the
// client gets the first chunk before the server ends; server-sent events; flushHeaders(); 'close' on the request and
// the response when the client goes away; write() and 'drain' (same output as Node)
import http from "node:http";

// writes 64 pieces of 64 KB, waiting for 'drain' whenever write() says so
class BigWriter {
  res: http.ServerResponse;
  piece: Buffer = Buffer.alloc(65536, 97);
  writes: number = 0;
  falses: number = 0;
  drains: number = 0;
  constructor(res: http.ServerResponse) {
    this.res = res;
  }
  more(): void {
    while (this.writes < 64) {
      this.writes++;
      if (!this.res.write(this.piece)) {
        this.falses++;
        this.res.once("drain", () => {
          this.drains++;
          this.more();
        });
        return;
      }
    }
    this.res.end(() => console.log("server: big done, write said wait:", this.falses > 0, "drains match:", this.drains === this.falses));
  }
}

const server = http.createServer((req, res) => {
  if (req.url === "/slow") {
    res.write("first;");
    setTimeout(() => {
      res.write("second;");
      setTimeout(() => res.end("last"), 150);
    }, 150);
  } else if (req.url === "/events") {
    res.writeHead(200, { "Content-Type": "text/event-stream", "Cache-Control": "no-cache" });
    res.flushHeaders();
    let n = 0;
    const timer = setInterval(() => {
      n++;
      res.write(`data: tick ${n}\n\n`);
    }, 50);
    req.on("close", () => {
      clearInterval(timer);
      console.log("server: client went away after", n >= 1 ? "some events" : "nothing", "ended:", res.writableEnded, "finished:", res.writableFinished);
    });
    res.on("close", () => console.log("server: response close"));
  } else if (req.url === "/big") {
    new BigWriter(res).more();
  } else {
    res.end("plain");
  }
});

server.listen(0, () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const t0 = Date.now();
  http.get({ hostname: "127.0.0.1", port: port, path: "/slow" }, (res) => {
    console.log("slow:", res.statusCode, res.headers["transfer-encoding"], res.headers["content-length"] ?? "-");
    const seen: string[] = [];
    let firstAt = -1;
    res.setEncoding("utf8");
    res.on("data", (c: string) => {
      if (firstAt < 0) firstAt = Date.now() - t0;
      seen.push(c);
    });
    res.on("end", () => {
      const total = Date.now() - t0;
      console.log("slow body:", seen.join(""), "first chunk early:", firstAt < total - 200, "chunks > 1:", seen.length > 1);
      events(port);
    });
  });
});

function events(port: number): void {
  const req = http.get({ hostname: "127.0.0.1", port: port, path: "/events" }, (res) => {
    console.log("events:", res.statusCode, res.headers["content-type"], res.headers["transfer-encoding"]);
    let text = "";
    res.setEncoding("utf8");
    res.on("data", (c: string) => {
      text += c;
      if (text.includes("tick 3")) {
        console.log("client: got", text.split("\n\n").filter((e) => e.length > 0).slice(0, 3).join(" | "));
        req.destroy();
        setTimeout(() => big(port), 100);
      }
    });
  });
  req.on("error", (e) => console.log("client error:", e.message));
}

function big(port: number): void {
  http.get({ hostname: "127.0.0.1", port: port, path: "/big" }, (res) => {
    let n = 0;
    res.on("data", (c: Buffer) => (n += c.length));
    res.on("end", () => {
      console.log("big received:", n);
      server.close(() => console.log("closed"));
    });
  });
}
