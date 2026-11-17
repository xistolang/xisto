// fs.watch: a directory's new entry, a file's change, close() and 'close', an unref'd (persistent: false) watcher, ENOENT
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

const d = fs.mkdtempSync(path.join(os.tmpdir(), "xisto-watch-"));
fs.writeFileSync(path.join(d, "old"), "1");

// a watcher that never sees anything and does not keep the program alive
fs.watch(os.tmpdir(), { persistent: false }, () => {});

try {
  fs.watch(path.join(d, "missing"));
} catch (e) {
  console.log((e as any).code, (e as any).syscall, (e as Error).message.replace(d, "DIR"));
}

// (FSEvents, Node's on macOS, also reports what happened just before a watcher starts: wait first)
setTimeout(() => {
  let sawNew = false;
  const w = fs.watch(d, (type: string, name: string) => {
    if (name !== "new" || sawNew) return;
    sawNew = true;
    console.log("dir: an event for", name);
    w.close();
  });
  w.on("close", () => {
    console.log("dir watcher closed");
    let sawChange = false;
    const fw = fs.watch(path.join(d, "old"), (type: string, name: string) => {
      if (sawChange) return;
      sawChange = true;
      console.log("file:", type, name);
      fw.close();
      fs.rmSync(d, { recursive: true });
    });
    setTimeout(() => fs.appendFileSync(path.join(d, "old"), "2"), 100);
  });
  setTimeout(() => fs.writeFileSync(path.join(d, "new"), "x"), 100);
}, 1200);
