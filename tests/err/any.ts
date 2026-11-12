// expect: cannot call 'on' on an any: its methods are not looked up by name
// (a method of an any is looked up by name at run time, but not with a function argument: a closure's signature is not checked)
const x: any = null;
x.on("data", (v: number) => v * 2);
