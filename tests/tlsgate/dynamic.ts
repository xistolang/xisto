let base = "http://127.0.0.1:1";
if (process.argv.length > 2) {
  base = process.argv[2];
}
const r = await fetch(base + "/x");
console.log(r.status);
