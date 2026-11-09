// a barrel: one file that re-exports the others
export * from "./circle.ts";
export { Square } from "./square.ts";
export { kindOf, KIND_LIMIT } from "./kinds.ts";
