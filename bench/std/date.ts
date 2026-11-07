// Date under load: parse 1M ISO timestamps, format 1M with toISOString, read local fields of 1M dates and build
// 1M dates from local parts (calendar math and the time zone on every one).
const base = Date.UTC(2020, 0, 1);
const stamps: string[] = [];
for (let i: int = 0; i < 1000000; i++) {
  stamps.push(new Date(base + Number(i * 61379)).toISOString());
}
let sum: number = 0;
for (const s of stamps) {
  sum += Date.parse(s) % 1000003;
}
let len: int = 0;
for (let i: int = 0; i < 1000000; i++) {
  len += new Date(base + Number(i * 997)).toISOString().length;
}
let fields: int = 0;
for (let i: int = 0; i < 1000000; i++) {
  const d = new Date(base + Number(i * 3600000));
  fields += d.getFullYear() + d.getMonth() + d.getDate() + d.getHours() + d.getDay();
}
let built: number = 0;
for (let i: int = 0; i < 1000000; i++) {
  built += new Date(2026, i % 12, (i % 28) + 1, i % 24, i % 60).getTime() % 1009;
}
console.log(sum, len, fields, built);
