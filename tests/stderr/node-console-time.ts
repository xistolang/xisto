// console.time / timeLog / timeEnd ("label: 1.234ms", normalized by the runner), the warnings for a label that is
// not there, console.assert ("Assertion failed" to stderr), console.trace (stderr: xisto's stack in the expected
// file), console.error and console.warn indented inside a console.group
console.time();
console.time("load");
console.time("load");
console.timeLog("load", "step", 1, { a: 1 });
console.timeEnd("load");
console.timeEnd();
console.timeEnd("nope");
console.timeLog("nope");
console.assert(true, "never printed");
console.assert(false);
console.assert(false, "x %s", "y", 3);
console.assert(0 > 1, "count:", 7);
console.group("grp");
console.error("error in group");
console.warn("warn\nin group");
console.log("log in group");
console.assert(false, "assert in group");
console.groupEnd();
console.countReset("zz");
console.log("done");
