class Opts { name: string = ""; async: boolean = false; }
interface I { async: boolean; delete?: string; }
const o: Opts = { name: "a", async: true };
const i: I = { async: true };
console.log(o.async, o.name, i.async);
