// node:http's IncomingMessage is a stream.Readable: the server reads the body with for await, the client pipes the
// response into a Writable (same output as Node)
import http from "node:http";
import { Writable } from "node:stream";

const server = http.createServer(async (req, res) => {
  let body = "";
  for await (const chunk of req) body += String(chunk);
  res.setHeader("Content-Type", "text/plain");
  res.end(body.toUpperCase());
});

server.listen(0, () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const req = http.request({ hostname: "127.0.0.1", port: port, path: "/up", method: "POST" }, (res) => {
    const parts: string[] = [];
    const sink = new Writable({
      write: (chunk: any, encoding: string, callback: (error?: Error | null) => void): void => {
        parts.push(String(chunk));
        callback();
      },
    });
    sink.on("finish", () => {
      console.log("piped:", res.statusCode, parts.join(""));
      server.close(() => console.log("closed"));
    });
    res.pipe(sink);
  });
  req.write("hello ");
  req.end("stream");
});
