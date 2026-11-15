// node:fs's callback API (fs.readFile(path, options?, cb) & co: the callback on a later turn of the event loop, err
// null when it worked) and fs.promises / node:fs/promises beyond text files (mkdir, readdir, stat, rm, cp...)
import fs from "node:fs";
import { readFile, mkdir, readdir, stat, lstat, rm, writeFile, copyFile, cp, rename, access, mkdtemp, realpath, symlink, readlink, chmod, truncate, utimes, appendFile, rmdir, unlink } from "node:fs/promises";
import { tmpdir } from "node:os";

const d = fs.mkdtempSync(tmpdir() + "/xisto-fs-cb-");
const rel = (p: string): string => p.split(d).join("<d>");
console.log("start");
fs.writeFile(d + "/a.txt", "abc", (err) => {
  console.log("written", err);
  fs.readFile(d + "/a.txt", "utf8", (err2, text) => {
    console.log("read", err2, text);
    fs.readFile(d + "/a.txt", (err3, buf) => {
      console.log("bytes", err3, buf.length, buf[0]);
      fs.stat(d + "/a.txt", (e4, st) => {
        console.log("stat", e4, st.size, st.isFile());
        fs.appendFile(d + "/a.txt", "def", { encoding: "utf8" }, (e) => {
          fs.readdir(d, { withFileTypes: true }, (e5, ents) => {
            console.log("dirents", e5, ents.map((x) => x.name + ":" + x.isFile()));
            fs.readFile(d + "/nope", { encoding: "utf8" }, (e6, t6) => {
              console.log("missing", e6 !== null, rel(e6 !== null ? e6.message : ""));
              fs.mkdir(d + "/x/y", { recursive: true }, (e7, made) => {
                console.log("mkdir", e7, made === d + "/x");
                fs.rename(d + "/a.txt", d + "/b.txt", (e8) => {
                  fs.access(d + "/b.txt", fs.constants.R_OK, (e9) => {
                    fs.copyFile(d + "/b.txt", d + "/c.txt", (e10) => {
                      fs.readdir(d, (e11, names) => {
                        console.log("names", e8, e9, e10, e11, names);
                        fs.unlink(d + "/c.txt", (e12) => {
                          fs.rmdir(d + "/x", (e13) => {
                            console.log("rmdir", e12, e13 !== null && (e13.message.startsWith("ENOTEMPTY") || e13.message.startsWith("EEXIST")));
                            promisesPart().then(() => fs.rm(d, { recursive: true }, (e14) => console.log("rm", e14, fs.existsSync(d))));
                          });
                        });
                      });
                    });
                  });
                });
              });
            });
          });
        });
      });
    });
  });
});
Promise.resolve().then(() => console.log("microtask"));
console.log("after");

async function promisesPart(): Promise<void> {
  await writeFile(d + "/p.txt", "pp");
  await appendFile(d + "/p.txt", "+");
  console.log(await readFile(d + "/p.txt", "utf8"), (await readFile(d + "/p.txt")).length, await fs.promises.readFile(d + "/p.txt", { encoding: "utf8" }));
  console.log((await fs.promises.readFile(d + "/p.txt")).toString("hex"));
  console.log((await mkdir(d + "/q/r", { recursive: true })) === d + "/q", await readdir(d));
  for (const e of await readdir(d, { withFileTypes: true })) console.log(e.name, e.isDirectory(), rel(e.parentPath));
  console.log((await stat(d + "/p.txt")).size, (await fs.promises.stat(d + "/p.txt")).isFile(), (await lstat(d + "/q")).isDirectory());
  try {
    await stat(d + "/zz");
  } catch (e: any) {
    console.log(e.code, e.syscall, rel(e.message));
  }
  try {
    await fs.promises.readdir(d + "/zz");
  } catch (e: any) {
    console.log(e.code, e.syscall);
  }
  await copyFile(d + "/p.txt", d + "/q/p2.txt");
  await cp(d + "/q", d + "/q2", { recursive: true });
  await rename(d + "/q2/r", d + "/q2/s");
  await access(d + "/q2/s");
  console.log(await readdir(d + "/q2"));
  const t = await mkdtemp(d + "/t-");
  console.log(rel(t).startsWith("<d>/t-"), rel(t).length);
  await symlink("p.txt", d + "/ln");
  console.log(await readlink(d + "/ln"), (await realpath(d + "/ln")).endsWith("/p.txt"));
  await chmod(d + "/p.txt", 0o640);
  await truncate(d + "/p.txt", 1);
  await utimes(d + "/p.txt", 10, 20);
  const st = await stat(d + "/p.txt");
  console.log((st.mode & 0o777).toString(8), st.size, st.mtimeMs);
  await unlink(d + "/ln");
  await rmdir(t);
  await rm(d + "/q", { recursive: true });
  await fs.promises.rm(d + "/q2", { recursive: true, force: true });
  console.log(await fs.promises.readdir(d));
}
