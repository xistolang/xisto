// expect: 'sq' is not exported by '../ok/lib/geometry.ts'
import { sq } from "../ok/lib/geometry.ts";
console.log(sq(2));
