// expect: runtime error: TypeError: 'b' is null here: a call made since it was checked set it back to null
// A variable narrowed to not null that a closure sets back to null before it is read: TypeScript keeps the narrowing
// (Node prints null here, and b.length would throw); xisto checks each such read and throws a TypeError naming it
// (before, the read was unchecked: a segfault). Tests against null still see it as it is.
function mk(a: string): string {
  return a + "1";
}
function f(v: string): void {
  let b: string | null = null;
  const ini = (): void => {
    b = null;
  };
  b = mk(v);
  if (b !== null) {
    ini();
    console.log(b !== null, b === null, b ?? "dflt");
    console.log(b);
  }
}
f("a");
