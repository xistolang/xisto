// RegExp under load: validate 400k emails, parse 300k log lines with groups, a global replace and a split
// over a 22 MB text. (redos.ts: the patterns that make backtracking engines explode.)
const users: string[] = [];
for (let i: int = 0; i < 400000; i++) {
  const name = "user" + String(i % 9973);
  users.push(i % 7 === 0 ? name + "@@bad" : name + "." + String(i % 13) + "@mail" + String(i % 17) + ".example.com");
}
const email = /^[\w.+-]+@[\w-]+(\.[\w-]+)+$/;
let valid: int = 0;
for (const u of users) {
  if (email.test(u)) {
    valid++;
  }
}
console.log("valid", valid);

const lines: string[] = [];
for (let i: int = 0; i < 300000; i++) {
  const status = i % 11 === 0 ? "500" : i % 5 === 0 ? "404" : "200";
  lines.push("10.0." + String(i % 256) + "." + String(i % 199) + ' - - [06/Nov/2026:10:' + String(10 + (i % 50)) + ':00] "GET /api/items/' + String(i) + ' HTTP/1.1" ' + status + " " + String(100 + (i % 900)));
}
const logLine = /^(\d+\.\d+\.\d+\.\d+) - - \[([^\]]+)\] "(\w+) ([^ ]+) HTTP\/[\d.]+" (\d{3}) (\d+)$/;
let errors: int = 0;
let bytes: number = 0;
for (const l of lines) {
  const m = logLine.exec(l);
  if (m !== null) {
    if (m[5] === "500") {
      errors++;
    }
    bytes += parseInt(m[6]);
  }
}
console.log("errors", errors, "bytes", bytes);

const text = lines.join("\n");
const masked = text.replace(/\d+\.\d+\.\d+\.\d+/g, "x.x.x.x");
console.log("masked", masked.length);
const parts = text.split(/\s+/);
console.log("parts", parts.length);

