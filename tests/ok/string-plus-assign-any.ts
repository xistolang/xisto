// s += x with x an any (an event listener's chunk): String(x), as s = s + x does; a Buffer gives its UTF-8 text
// (as JavaScript's toString), numbers and booleans their text; typed string += string is unchanged
import { EventEmitter } from "node:events";
import { spawn } from "node:child_process";

const e = new EventEmitter();
let o = "";
e.on("data", (d) => (o += d));
e.on("data", (d: any) => {
  o += d;
});
e.emit("data", Buffer.from("héllo"));
e.emit("data", 5);
e.emit("data", true);
console.log(o);

let x: any = Buffer.from([0x6f, 0x6b]);
let s = "<";
s += x;
s += ">";
console.log(s, s.length);

const p = spawn("echo", ["from child"]);
let out = "";
p.stdout.on("data", (d) => (out += d));
p.on("close", (code) => console.log(JSON.stringify(out), code));
