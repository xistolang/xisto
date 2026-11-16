// expect: 'F' is a function: 'new' makes objects of classes only
function F(this: { x: number }): void { this.x = 1; }
const f = new (F as any)();
console.log(f);
