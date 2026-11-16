// net.Socket.pipe: an echo server (c.pipe(c)) and a proxy (client socket piped to a backend socket and back)
import * as net from "node:net";

const echo = net.createServer((c: net.Socket) => {
  c.on("pipe", (src: net.Socket) => console.log("pipe event", src === c));
  c.pipe(c);
});
echo.listen(0, "127.0.0.1", () => {
  const ea = echo.address();
  const eport = ea !== null && typeof ea === "object" ? ea.port : 0;
  // the proxy: what a client sends goes to the echo server, and its answer back
  const proxy = net.createServer((c: net.Socket) => {
    const up = net.connect(eport, "127.0.0.1");
    c.pipe(up);
    up.pipe(c);
  });
  proxy.listen(0, "127.0.0.1", () => {
    const pa = proxy.address();
    const pport = pa !== null && typeof pa === "object" ? pa.port : 0;
    const c = net.connect(pport, "127.0.0.1", () => c.write("ping"));
    let got = "";
    c.setEncoding("utf8");
    c.on("data", (d: string) => {
      got += d;
      if (got === "ping") c.end("!");
    });
    c.on("end", () => {
      console.log("client got", got);
      proxy.close(() => console.log("proxy closed"));
      echo.close(() => console.log("echo closed"));
    });
  });
});
