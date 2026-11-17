// A tuple type with a string literal member: its name ([string | "TRIADO", number | null]) goes into the
// generated C as a string and must be escaped.
type Kind = [string | "TRIADO", number | null];
const kinds: Kind[] = [["TRIADO", 1], ["OUTRO", null]];
const seen: any = { n: kinds.length };
for (const k of kinds) console.log(k[0], k[1], k);
console.log(seen.n);
