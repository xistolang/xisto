import { Tool as Direct } from "./lib.ts";
import { Tool } from "./index.ts";
export function checkTwo(): string { return `${new Direct().label} ${new Tool().label} ${new Direct() instanceof Tool}`; }
