import { register, registry } from "./registry.ts";
import { A_NAME } from "./a.ts";
try {
  register("b", registry.get("a") ?? -1);
} catch (e) {
  console.log("never");
}
console.log("b loaded after", A_NAME);
export const B_DONE = true;
