// expect: cannot call 'trim' on an any: its methods are not looked up by name
const x: any = " a ";
console.log(x.trim());
