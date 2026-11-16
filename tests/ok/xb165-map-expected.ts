// XB-165: map takes its callback's result type from the array type expected (an (R | null)[] where () => null maps)
interface R { v: number }
const ys = [1, 2, 3];
const xs: (R | null)[] = ys.map(() => null);
const zs: (R | null)[] = ys.map((y) => (y % 2 === 1 ? { v: y } : null));
const ws: (R | null)[] = ys.map((y) => { if (y > 1) return { v: y * 10 }; return null; });
const ss: (string | null)[] = ys.map((y) => (y === 2 ? null : `s${y}`));
const ns: (string | null)[] = ys.map(() => null);
function rows(): (R | null)[] { return ys.map(() => null); }
console.log(xs.length, xs[0], zs.map((z) => (z ? z.v : -1)).join(","), ws.map((w) => (w ? w.v : 0)).join(","));
console.log(ss.join("|"), ns.length, ns[2], rows().length);
const nn: (number | null)[] = ys.map(() => null);
const mm: (number | null)[] = ys.map((y) => (y === 2 ? null : y * 1.5));
console.log(nn.length, nn[0], mm.join(","));
