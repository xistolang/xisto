// The fs file descriptor API: writeSync, and the callback calls open, read, write, close, exists
import * as fs from "node:fs";
import { open, close, read, write, exists, writeSync, openSync, closeSync, readSync } from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "xisto-fd-"));
const file = path.join(dir, "a.txt");

// writeSync: text (at a position), a buffer (offset and length), and errors
const w = openSync(file, "w");
console.log("text", writeSync(w, "héllo "));
console.log("buffer", writeSync(w, Buffer.from("0123456789"), 2, 5));
console.log("at position", writeSync(w, "H", 0));
console.log("latin1", writeSync(w, "é", null, "latin1"));
try {
  writeSync(w, Buffer.from("abc"), 4);
} catch (e) {
  console.log((e as Error).name, (e as any).code, (e as Error).message);
}
const spans: int[][] = [[1, 5], [-1, 1], [0, -2]];
for (const span of spans) {
  try {
    writeSync(w, Buffer.from("abc"), span[0], span[1]);
  } catch (e) {
    console.log((e as Error).name, (e as any).code, (e as Error).message);
  }
}
try {
  read(w, Buffer.alloc(3), 9, 1, 0, (e: Error | null, n: int, b: Uint8Array) => {});
} catch (e) {
  console.log((e as Error).name, (e as any).code, (e as Error).message);
}
closeSync(w);
console.log(JSON.stringify(fs.readFileSync(file).toString("latin1")));
try {
  writeSync(9999, "x");
} catch (e) {
  console.log((e as any).code, (e as any).syscall, (e as Error).message);
}
const r0 = openSync(file, "r");
const head = Buffer.alloc(4);
console.log("readSync", readSync(r0, head, 0, 4, 1), head.toString());
closeSync(r0);
console.log("stdout", writeSync(1, "written to fd 1\n"));

// the callback calls, one after another
exists(file, (yes: boolean) => {
  console.log("exists", yes);
  exists(path.join(dir, "nope"), (no: boolean) => {
    console.log("exists nope", no);
    open(file, "r+", (err: Error | null, fd: int) => {
      console.log("open", err, fd > 2);
      const buf = Buffer.alloc(8);
      read(fd, buf, 0, 5, 0, (err2: Error | null, n: int, b: Uint8Array) => {
        console.log("read", err2, n, buf.toString("latin1", 0, n), b === buf);
        write(fd, "XY", 1, (err3: Error | null, written: int, data: string | Uint8Array) => {
          console.log("write text", err3, written, data);
          write(fd, Buffer.from("!!"), 0, 1, null, (err4: Error | null, written2: int) => {
            console.log("write buffer", err4, written2);
            close(fd, (err5: Error | null) => {
              console.log("close", err5, JSON.stringify(fs.readFileSync(file).toString("latin1")));
              close(fd, (err6: Error | null) => {
                console.log("close again", err6 !== null ? (err6 as any).code : "none");
                open(path.join(dir, "missing", "x"), (err7: Error | null, fd7: int) => {
                  console.log("open missing", err7 !== null ? err7.message.replace(dir, "DIR") : "");
                  try {
                    open(file, "zz", (e: Error | null, f: int) => {});
                  } catch (e) {
                    console.log((e as Error).name, (e as any).code, (e as Error).message);
                  }
                  fs.rmSync(dir, { recursive: true });
                });
              });
            });
          });
        });
      });
    });
  });
});
