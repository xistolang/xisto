import { readFileSync } from "node:fs";
const text = readFileSync(process.argv[2], "utf8");
let n = 0;
for (const line of text.split("\n")) if (line.includes("ERROR")) n++;
console.log(n);
