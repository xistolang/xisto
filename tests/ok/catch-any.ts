// catch (error: any): the value thrown, read by name (a driver's error class and its code); a field it lacks reads as undefined
class DbError extends Error {
  errorNum: number;
  constructor(m: string, n: number) { super(m); this.errorNum = n; }
}
function f(k: number): void {
  if (k === 1) throw new DbError("ORA-1", 1017);
  if (k === 2) throw new Error("plain");
}
for (const k of [1, 2]) {
  try {
    f(k);
  } catch (error: any) {
    const code = error?.errorNum || error?.code || 0;
    console.log(error.message, code, error?.code === 11000);
  }
}
