// XB-150: (boolean | null)[] holds the boolean's null sentinel; printing, JSON, indexOf/includes(null), join as Node
const bs: (boolean | null)[] = [true, null, false];
console.log(bs);
console.log(JSON.stringify(bs));
console.log(bs.indexOf(null), bs.includes(null), bs.includes(false));
console.log(bs.join(","));
bs.push(null);
console.log(bs.length, bs[1] === null, bs[0]);
interface L { X: string | null }
function num(s: string | null): number | null { return s === null ? null : Number(s); }
const ls: L[] = [{ X: "1" }, { X: null }, { X: "2.5" }];
const ns = ls.map(l => num(l.X));
console.log(ns, JSON.stringify(ns));
const flags = ls.map(l => l.X === null ? null : l.X === "1");
console.log(flags);
const bs2: (boolean | null)[] = [true, null, false];
const x: boolean | null = bs2[1];
console.log(x, x === null, bs[0] === true, bs[2] ?? true, bs[1] ?? true);
bs[1] = true; bs[0] = null;
console.log(bs, bs.length);
for (const b of bs) console.log(b === null ? "N" : b ? "T" : "F");
console.log(bs.filter(b => b !== null).length, bs.map(b => String(b)).join("|"));
bs.forEach((b, i) => console.log(i, b));
console.log(bs.slice(1), [...bs].reverse(), bs.indexOf(true), bs.includes(null));
const e: (boolean | null)[] = [];
e.push(null); e.push(false);
console.log(e, e.pop(), e);
interface R { flags: (boolean | null)[]; name: string }
const r: R = { flags: [null, true], name: "r" };
console.log(r, JSON.stringify(r));
const parsed = JSON.parse('{"flags":[true,null,false],"name":"p"}') as R;
console.log(parsed.flags, parsed.flags[1] === null);
const u: unknown = bs;
console.log(u, JSON.stringify(u), Array.isArray(u));
const nn = bs.map(b => b === null ? null : !b);
console.log(nn);
const cnt = bs.reduce((a, b) => a + (b === true ? 1 : 0), 0);
console.log(cnt);
