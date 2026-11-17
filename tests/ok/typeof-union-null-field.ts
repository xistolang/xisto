// build: --tsc-checked
// A field typed A | B | null narrowed by typeof: the test excludes null as well, so the field reads as that member
// (and after `if (typeof f === "number") return`, the next typeof test narrows string | null to string).
interface V { from: number | string | null }
interface B { key: number | string | null; label?: string }

function toDate(b: B): string {
  if (typeof b.key === 'number') return new Date(b.key).toISOString();
  if (typeof b.key === 'string') return 'str:' + b.key.toUpperCase();
  return 'none';
}

function notNumber(b: B): string {
  if (typeof b.key !== 'number') return b.key === null ? 'null' : 'text ' + b.key;
  return 'n=' + (b.key + 1);
}

class Holder {
  from: number | string | null = null;
  width(): number {
    if (typeof this.from === "number") return this.from * 2;
    if (typeof this.from === "string") return this.from.length;
    return -1;
  }
}

const vs: V[] = [{ from: 3 }, { from: "abc" }, { from: null }];
for (const v of vs) {
  let n = 0;
  if (typeof v.from === "number") n = v.from;
  let s = "";
  if (typeof v.from === "string") s = v.from;
  else s = "-";
  console.log(n, s, v.from === null);
}
console.log(toDate({ key: 0 }), toDate({ key: "x" }), toDate({ key: null }));
console.log(notNumber({ key: 4 }), notNumber({ key: "y" }), notNumber({ key: null }));
const h = new Holder();
console.log(h.width());
h.from = 21;
console.log(h.width());
h.from = "four";
console.log(h.width());
