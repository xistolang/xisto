// xisto:fs/promises (node:fs/promises): the promise flavour of node:fs for text files, over xisto:fs.
// readFile needs an encoding (text): without one Node gives a Buffer, which is not here yet.
// Errors are Node's: "ENOENT: no such file or directory, open '<path>'".
import { appendFileSync, readFileSync, unlinkSync, writeFileSync } from "xisto:fs";

/**
 * The whole file as text.
 * @example const pem = await readFile("key.pem", "utf-8");
 */
export async function readFile(path: string, encoding: string): Promise<string> {
  if (encoding === "utf8" || encoding === "utf-8") {
    return readFileSync(path, "utf8");
  }
  return readFileSync(path).toString(encoding);
}

/** Writes text to a file, replacing it (UTF-8). */
export async function writeFile(path: string, data: string): Promise<void> {
  writeFileSync(path, data);
}

/** Adds text at the end of a file, creating it when missing (UTF-8). */
export async function appendFile(path: string, data: string): Promise<void> {
  appendFileSync(path, data);
}

/** Removes a file. */
export async function unlink(path: string): Promise<void> {
  unlinkSync(path);
}
