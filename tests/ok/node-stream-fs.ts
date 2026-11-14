// fs.createReadStream (chunks, start/end, encoding, a missing file) and fs.createWriteStream (write, end, flags a),
// and a file copied with pipeline.
import { createReadStream, createWriteStream, writeFileSync, readFileSync, unlinkSync } from "node:fs";
import { pipeline } from "node:stream";

function log(...a: any[]): void {
  console.log(a.join(" "));
}
const dir = "/tmp";
const src = dir + "/xisto-stream-src.txt";
const dst = dir + "/xisto-stream-dst.txt";
let text = "";
for (let i = 0; i < 2000; i++) text += "line " + i + "\n";
writeFileSync(src, text);

function step1(next: () => void): void {
  const r = createReadStream(src, { highWaterMark: 4096 });
  let n = 0;
  let bytes = 0;
  r.on("open", (fd: number) => log("open", typeof fd));
  r.on("ready", () => log("ready"));
  r.on("data", (c: Buffer) => {
    n++;
    bytes += c.length;
  });
  r.on("end", () => log("end", n, bytes, r.bytesRead));
  r.on("close", () => {
    log("close");
    next();
  });
}

function step2(next: () => void): void {
  const r = createReadStream(src, { start: 5, end: 14, encoding: "utf8" });
  const parts: string[] = [];
  r.on("data", (c: string) => parts.push(c));
  r.on("end", () => {
    log("range", JSON.stringify(parts.join("")));
    next();
  });
}

function step3(next: () => void): void {
  const w = createWriteStream(dst);
  w.on("open", () => log("w open"));
  w.on("finish", () => log("w finish", w.bytesWritten));
  w.on("close", () => {
    log("w close", readFileSync(dst, "utf8"));
    next();
  });
  log("write1", w.write("hello "));
  log("write2", w.write(Buffer.from("world")));
  w.end("!\n");
}

function step4(next: () => void): void {
  pipeline(createReadStream(src), createWriteStream(dst), (err?: Error | null) => {
    log("copy", err === null || err === undefined ? "ok" : err.message, readFileSync(dst, "utf8") === text);
    next();
  });
}

function step5(next: () => void): void {
  const r = createReadStream(dir + "/xisto-no-such-file.txt");
  r.on("error", (e: any) => log("error", e.code, e.syscall, e.message));
  r.on("close", () => {
    log("close5");
    next();
  });
}

function step6(next: () => void): void {
  const w = createWriteStream(dst, { flags: "a" });
  w.end("appended\n");
  w.on("close", () => {
    const lines = readFileSync(dst, "utf8").split("\n");
    log("append", lines[lines.length - 2]);
    unlinkSync(src);
    unlinkSync(dst);
    next();
  });
}

step1(() => step2(() => step3(() => step4(() => step5(() => step6(() => log("all done")))))));
