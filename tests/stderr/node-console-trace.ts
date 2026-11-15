// console.trace: "Trace: message" and the stack to stderr (xisto's stack: the expected stderr is xisto's, Node
// prints its own frames), indented inside a console.group
function inner(n: number): void {
  console.trace("at depth %d", n);
}
console.log("before");
inner(1);
console.group("g");
console.trace();
console.groupEnd();
console.log("after");
