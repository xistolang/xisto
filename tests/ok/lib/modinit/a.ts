import { register } from "./registry.ts";
const local = [3, 1, 2];
let total = 0;
for (const x of local) total += x;
register("a", total);
export const A_NAME = `a-${total}`;
console.log("a loaded", A_NAME);
