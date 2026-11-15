// node:tty: isatty on what is not a terminal (the test's stdin, stdout to a file), the stream properties of
// process.stdout/stderr off a terminal (tty.WriteStream needs a terminal in Node: getColorDepth is checked by hand)
import * as tty from "node:tty";

console.log(tty.isatty(0), tty.isatty(1), tty.isatty(2), tty.isatty(99), tty.isatty(-1));
console.log(process.stdout.isTTY === true, process.stderr.isTTY === true, process.stdin.isTTY === true);
console.log("columns:", process.stdout.columns === undefined, "rows:", process.stdout.rows === undefined);
