// expect: 'value' is not a field of every member
type R = { ok: true; value: string } | { ok: false; error: string };
const t: [R, number] = [{ ok: false, error: "e" }, 1];
if (t[0].ok) console.log(t[0].value);
console.log(t[0].value);
