// a Buffer that came as unknown (a reflected call's arguments), checked at run time by `as`
function size(v: unknown): number {
  const b = v as Buffer;
  return b.length;
}
const xs: unknown[] = [Buffer.from("abc"), Buffer.from("hello")];
console.log(size(xs[0]), size(xs[1]));
