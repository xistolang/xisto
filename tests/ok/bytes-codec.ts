// base64 / base64url / hex both ways over every length 0..70 and a pseudo-random content, against Node
let seed: int = 12345;
let report = "";
for (let n: int = 0; n <= 70; n++) {
  const b = new Uint8Array(n);
  for (let i: int = 0; i < n; i++) {
    seed = (Math.imul(seed, 1103515245) + 12345) & 0x7fffffff;
    b[i] = seed >> 16;
  }
  const buf = Buffer.from(b);
  const s64 = buf.toString("base64");
  const u64 = buf.toString("base64url");
  const hx = buf.toString("hex");
  const ok = Buffer.from(s64, "base64").equals(buf) && Buffer.from(u64, "base64url").equals(buf) && Buffer.from(hx, "hex").equals(buf) &&
  Uint8Array.fromBase64(b.toBase64()).join() === b.join() && Uint8Array.fromHex(b.toHex()).join() === b.join();
  report += n % 10 === 0 ? `\n${n}:${s64.slice(0, 12)}:${hx.slice(0, 8)}:${ok}` : ok ? "." : "X";
}
console.log(report);
console.log(Buffer.from(" aGVs\nbG8g d29y bGQ= ", "base64").toString(), Buffer.from("aGVsbG8gd29ybGQ", "base64").toString());
