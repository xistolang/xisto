// XB-087: JavaScript's -0 from int operations, where a number sees it (division, Number(), console.log): an int
// slot holds 0 for it, but the expression and a variable written once keep the sign, as Node does.
function f(x: int): number { return 1 / (x * -3); }
function g(x: int): number { const y = x * -3; return 1 / y; }
function h(x: int): number { return 1 / -x; }
function m(x: int): number { return 1 / (x % 2); }
function p(v: number): number { return 1 / v; }
const a: int = Math.trunc(Number(process.argv.length > 99 ? "1" : "0"));
const b = a * -1;
console.log(f(0), g(0), h(0), m(-4), m(-3), 1 / b, Math.sign(Number(b)), String(b), `${b}`, [b].join(), b.toString());
console.log(Math.atan2(Number(a * -1), -1), 1 / Number(a * -5), p(Number(a * -2)), p(Number(b)));
let s: int = 0;
for (let i = 0; i < 3; i++) s += i * -1;
console.log(1 / s, 1 / (s * 0), s * 0);
const k = -a;
console.log(1 / k, a * -1, -a, Math.trunc(-0.5), Math.round(-0.2), Math.ceil(-0.5), Math.floor(-0.0), Math.round(-0.5), Math.round(0.4));
console.log(Math.min(a * -1, 0), Math.max(a * -1, 0), 1 / Math.min(0, a * -1), 1 / (a * -1 + 0), 1 / ((a * -1) - 0), 1 / ((a * -1) + (a * -1)));
const n = Math.trunc(-0.3);
console.log(n, 1 / n, 7 * -1, (a + 1) * -1, -5 % 5, 1 / (-5 % 5), 1 / (5 % 5), 1 / (-5 % -1));
// the sign cannot be seen: plain int arithmetic
console.log(1 / (Number(a * -1) / 2 + Number(a)), 1 / (Number(a * -1) / 2 - Number(a)), 1 / (Number(a * -1) * 3), 1 / (0 - Number(a * -1)), 1 / (Number(a * -1) - 0));
// a variable written again keeps 0 (int slot): the same as Node only when the sign is not seen
let w = a * -1;
w = w + 1;
console.log(w, 1 / (w - 1));
