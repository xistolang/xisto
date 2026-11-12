// typeof x.m === 'string' ? x.m : '' with x any: the result is a string (its methods can be called), as TypeScript narrows it
function codes(error: any): string {
  const message = typeof error?.message === "string" ? error.message : "";
  return message.split(" ")[0];
}
console.log(codes(new Error("ORA-1 x")), codes(null));
