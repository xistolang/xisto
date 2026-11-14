// toFixed: the exact decimal value rounded half up in magnitude, for every digits count
let seed = 12345;
function rnd(): number {
  seed = (seed * 1103515245 + 12345) % 2147483648;
  return seed / 2147483648;
}
const special: number[] = [0, -0, 0.5, 1.5, 2.5, -2.5, 0.125, 0.375, 1.005, 1.045, 10.235, 123.456, -0.0049, -0.005, 0.0049999999999999999,
  1e-7, 5e-324, 2.2250738585072014e-308, 9007199254740991, 9007199254740992, 9007199254740993, 4503599627370495.5,
  1e20, 1e21, 123456789012345680000, 0.1, 0.2, 0.30000000000000004, 1 / 3, 2 / 3, 99.995, 99.9949999, 0.045, 1.45, 8.345, -1.005];
const out: string[] = [];
for (const x of special) for (let d = 0; d <= 25; d++) out.push(x.toFixed(d));
for (let i = 0; i < 20000; i++) {
  const sign = rnd() < 0.5 ? -1 : 1;
  const e = Math.floor(rnd() * 40) - 20;
  const x = sign * rnd() * Math.pow(10, e);
  const d = Math.floor(rnd() * 24);
  out.push(x.toFixed(d));
  const t = Math.round(rnd() * 100000) / 8; // ties on eighths
  out.push(t.toFixed(2), (-t).toFixed(2), t.toFixed(1), (t / 1000).toFixed(4));
}
let h = 0;
for (const s of out) for (let k = 0; k < s.length; k++) h = (h * 31 + s.charCodeAt(k)) % 1000000007;
console.log(out.length, h);
console.log(out.slice(0, 120).join(" "));
