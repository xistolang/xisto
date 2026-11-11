// fs/promises (node:fs/promises): readFile with an encoding, writeFile, appendFile, unlink and their ENOENT errors
import { readFile, writeFile, appendFile, unlink } from "fs/promises";

async function main(): Promise<void> {
  const f = "build/fs-promises-test.txt"; // (the suite runs from the repository root)
  await writeFile(f, "line one\n");
  await appendFile(f, "línea dos ✓\n");
  const text = await readFile(f, "utf-8");
  console.log(JSON.stringify(text), text.length);
  console.log((await readFile(f, "utf8")).split("\n").length);
  console.log(await readFile(f, "base64"));
  console.log(await readFile(f, "hex"));
  await unlink(f);
  try {
    await readFile(f, "utf-8");
    console.log("no error");
  } catch (e) {
    console.log("read:", (e as Error).message);
  }
  try {
    await unlink(f);
  } catch (e) {
    console.log("unlink:", (e as Error).message);
  }
  const pending = readFile("/etc/hosts", "utf-8");
  const h = await pending;
  console.log(h.length > 0);
}
main();
