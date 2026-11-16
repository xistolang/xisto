// process.exitCode: empty until it is set (null in xisto, undefined on Node); the status logic is unchanged
console.log(process.exitCode ?? "none", process.exitCode === undefined);
process.exitCode = 3;
console.log(process.exitCode, process.exitCode ?? "none");
const c: number = process.exitCode ?? 0;
console.log(c + 1);
process.exitCode = 0;
console.log(process.exitCode);
