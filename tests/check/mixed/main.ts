// one of each kind of change xisto check counts; the uses of what failed are not counted again
import { readFileSync } from "node:fs";
import { Injectable } from "@acme/missing";
import { helper } from "./lib/helper";

function twice(x) { return x * 2; }
const loose: any = 1;

@Injectable()
export class Service {
  run(): string { return helper(2); }
}

function broken( {
  return 1;
}

const n: int = 3.5;
label: for (let i = 0; i < 2; i++) break label;
console.log(twice(1), readFileSync("x", "utf8"), n, loose, broken());
