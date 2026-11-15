// node:fs, synchronous: mkdirSync (recursive), readdirSync (withFileTypes, recursive), statSync/lstatSync, rmSync,
// renameSync, copyFileSync, cpSync, symlinkSync/readlinkSync, realpathSync, chmodSync, truncateSync, utimesSync,
// accessSync, options objects of readFileSync/writeFileSync, and Node's errors (message, code, errno, syscall, path)
import * as fs from "node:fs";
import { mkdirSync, writeFileSync, readdirSync, statSync, rmSync, existsSync, readFileSync, fstatSync, openSync, closeSync } from "node:fs";
import os from "node:os";

const d = fs.mkdtempSync(os.tmpdir() + "/xisto-fs-sync-");
const rel = (p: string): string => p.split(d).join("<d>");
console.log(fs.statSync(d).isDirectory(), rel(d + "/x"));
console.log(mkdirSync(d + "/a/b/c", { recursive: true }) === d + "/a", fs.mkdirSync(d + "/a/b", { recursive: true }) === undefined);
fs.mkdirSync(d + "/m", 0o755);
writeFileSync(d + "/a/f.txt", "hello");
fs.writeFileSync(d + "/a/g.bin", new Uint8Array([1, 2, 3]));
fs.appendFileSync(d + "/a/f.txt", " world", "utf8");
fs.writeFileSync(d + "/a/h.txt", "line\n", { encoding: "utf8", flag: "a" });
fs.writeFileSync(d + "/a/h.txt", "más\n", { flag: "a" });
fs.writeFileSync(d + "/a/l1.txt", "café", "latin1");
console.log(readFileSync(d + "/a/h.txt", "utf8"), readFileSync(d + "/a/l1.txt").length);
console.log(readdirSync(d + "/a"), fs.readdirSync(d, { recursive: true }));
for (const e of fs.readdirSync(d + "/a", { withFileTypes: true })) console.log(e.name, e.isFile(), e.isDirectory(), e.isSymbolicLink(), rel(e.parentPath));
for (const e of fs.readdirSync(d, { withFileTypes: true, recursive: true })) console.log(rel(e.parentPath), e.name);

const st = statSync(d + "/a/f.txt");
console.log(st.size, st.isFile(), st.isDirectory(), st.isSymbolicLink(), (st.mode & 0o777).toString(8), st.mtime instanceof Date, st.mtimeMs > 0, st.nlink);
console.log(fs.statSync(d + "/nope", { throwIfNoEntry: false }) === undefined, fs.lstatSync(d + "/nope", { throwIfNoEntry: false }) === undefined);
console.log(readFileSync(d + "/a/f.txt", { encoding: "utf8" }), readFileSync(d + "/a/g.bin").length, readFileSync(d + "/a/g.bin", { flag: "r" })[2], fs.readFileSync(d + "/a/f.txt", "utf8"));
const fd = openSync(d + "/a/f.txt", "r");
console.log(fstatSync(fd).size, fstatSync(fd).isFile());
closeSync(fd);

fs.renameSync(d + "/a/f.txt", d + "/a/r.txt");
fs.copyFileSync(d + "/a/r.txt", d + "/a/i.txt");
fs.cpSync(d + "/a", d + "/z", { recursive: true });
fs.cpSync(d + "/a/i.txt", d + "/z/j.txt");
console.log(fs.readdirSync(d + "/z"), fs.readFileSync(d + "/z/j.txt", "utf8"));
fs.symlinkSync("r.txt", d + "/a/link");
console.log(fs.readlinkSync(d + "/a/link"), fs.lstatSync(d + "/a/link").isSymbolicLink(), fs.statSync(d + "/a/link").isSymbolicLink());
console.log(fs.realpathSync(d + "/a/link") === fs.realpathSync(d) + "/a/r.txt", fs.realpathSync(d + "/a/b/../link") === fs.realpathSync(d + "/a/r.txt"));
fs.chmodSync(d + "/a/r.txt", 0o600);
console.log((fs.statSync(d + "/a/r.txt").mode & 0o777).toString(8));
fs.chmodSync(d + "/a/r.txt", "644");
console.log((fs.statSync(d + "/a/r.txt").mode & 0o777).toString(8));
fs.truncateSync(d + "/a/r.txt", 2);
console.log(JSON.stringify(fs.readFileSync(d + "/a/r.txt", "utf8")));
fs.utimesSync(d + "/a/r.txt", 1000, 2000.5);
console.log(fs.statSync(d + "/a/r.txt").mtimeMs, fs.statSync(d + "/a/r.txt").atime.toISOString());
fs.utimesSync(d + "/a/r.txt", new Date(5000), new Date(6000));
console.log(fs.statSync(d + "/a/r.txt").mtime.getTime());
fs.accessSync(d, fs.constants.R_OK | fs.constants.W_OK);
fs.accessSync(d + "/a/r.txt");
console.log(fs.constants.F_OK, fs.constants.R_OK, fs.constants.W_OK, fs.constants.X_OK, fs.constants.COPYFILE_EXCL, fs.constants.S_IFDIR);

const fails: (() => void)[] = [
  () => fs.rmSync(d + "/a"),
  () => fs.rmSync(d + "/nope"),
  () => fs.rmdirSync(d + "/a"),
  () => fs.accessSync(d + "/nope"),
  () => fs.renameSync(d + "/nope", d + "/x"),
  () => fs.copyFileSync(d + "/a/i.txt", d + "/a/r.txt", fs.constants.COPYFILE_EXCL),
  () => fs.symlinkSync("x", d + "/a/i.txt"),
  () => { fs.mkdirSync(d + "/a"); },
  () => { fs.mkdirSync(d + "/a/i.txt/q", { recursive: true }); },
  () => fs.cpSync(d + "/a", d + "/q"),
  () => { fs.readdirSync(d + "/nope"); },
  () => { fs.readdirSync(d + "/a/i.txt"); },
  () => { fs.statSync(d + "/nope"); },
  () => { fs.lstatSync(d + "/nope"); },
  () => { fs.readlinkSync(d + "/a/i.txt"); },
  () => { fs.readFileSync(d + "/nope"); },
  () => { fs.readFileSync(d + "/nope", "utf8"); },
  () => { fs.readFileSync(d + "/a"); },
  () => fs.writeFileSync(d + "/nope/x", "x"),
  () => fs.unlinkSync(d + "/nope"),
  () => fs.rmdirSync(d + "/a/i.txt"),
  () => fs.chmodSync(d + "/nope", 0o644),
  () => fs.truncateSync(d + "/nope"),
  () => fs.utimesSync(d + "/nope", 1, 1),
  () => { fs.mkdtempSync(d + "/nope/t-"); },
  () => { openSync(d + "/nope", "r"); },
];
for (const f of fails) {
  try {
    f();
    console.log("no error");
  } catch (e: any) {
    const m: string = e.message;
    const p: string = e.path ?? "";
    console.log(rel(m), e.code, e.errno === undefined ? "-" : e.errno < 0, e.syscall ?? "-", rel(p), e instanceof Error);
  }
}
try {
  fs.renameSync(d + "/nope", d + "/x");
} catch (e: any) {
  const dest: string = e.dest;
  console.log(rel(dest), e.errno === -2);
}
rmSync(d + "/a/i.txt");
rmSync(d + "/nope", { force: true });
fs.rmdirSync(d + "/m");
console.log(existsSync(d + "/a/i.txt"), existsSync(d + "/m"));
rmSync(d, { recursive: true, force: true });
console.log(existsSync(d));
