// expect: runtime error: uncaught Error: boom
function f(): void {
  throw new Error("boom");
}
f();
