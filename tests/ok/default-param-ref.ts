// a default that reads an earlier parameter reads the call's own argument, not a name of the caller
function span(a: Uint8Array, start: int = 0, end: int = a.length): int {
  return end - start;
}
class Box {
  bytes: Uint8Array = new Uint8Array(4);
}
function caller(): void {
  const b = new Uint8Array(7);
  const a = new Uint8Array(100); // the caller's own a: not the one the default reads
  console.log(span(b), span(b, 2), span(b, 2, 3), a.length);
  const box = new Box();
  console.log(span(box.bytes), span(box.bytes, 1));
}
caller();
