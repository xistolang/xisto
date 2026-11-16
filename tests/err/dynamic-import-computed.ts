// expect: import() takes a string literal: a native binary knows its modules when it is built
async function load(name: string): Promise<void> {
  const m = await import(name);
}
load("./x.ts");
