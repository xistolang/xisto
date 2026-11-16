// expect: 'greet' declares 'this': call it as greet.call(obj, ...) or greet.apply(obj, [...])
interface P { name: string }
function greet(this: P, g: string): string { return g + this.name; }
console.log(greet("x"));
