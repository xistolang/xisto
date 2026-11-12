// a field of an object literal with methods typed by a const object literal (as const) of the module
const MSG = {
  TIPOS: "tipos",
  OUTRO: "x",
} as const;
export const RT = {
  M: MSG,
  hello(): string { return this.M.TIPOS; },
};
console.log(RT.M.TIPOS, RT.hello());
