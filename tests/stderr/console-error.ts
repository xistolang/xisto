// console.error and console.warn write to stderr, log/info/debug to stdout, formatted as Node formats them
class Point { x: number; y: number; constructor(x: number, y: number) { this.x = x; this.y = y; } }
const point = new Point(1, -0);
console.log("out", 1, [1, 2, 3]);
console.error("err", 42, point);
console.info("info", true);
console.warn("warn", 1.5, "text", null);
console.debug("debug", [1.5, -2]);
console.error();
console.log("end");
