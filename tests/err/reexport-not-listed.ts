// expect: 'notReexported' is not exported by '../ok/lib/barrel/index.ts'
import { notReexported } from "../ok/lib/barrel/index.ts";
console.log(notReexported());
