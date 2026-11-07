// x! and (T | null) as T are checked in xisto: null is a TypeError (TypeScript would carry on with null)
function pick(n: int): string | null {
  return n > 1 ? `#${n}` : null;
}
try {
  console.log(pick(0)!.length);
} catch (e) {
  console.log("caught:", e.message);
}
try {
  const s = pick(1) as string;
  console.log(s);
} catch (e) {
  console.log("caught:", e.message);
}
