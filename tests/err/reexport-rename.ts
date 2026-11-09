// expect: renaming in a re-export ('Square as Box') is not supported
export { Square as Box } from "../ok/lib/barrel/square.ts";
console.log(1);
