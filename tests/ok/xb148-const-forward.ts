// XB-148: module consts read by functions and arrows declared before them (JavaScript checks only at the call)
const handlers = { ping: () => reply("pong"), count: () => table.size };
const a = () => b(2) + 1;
function viaFn(): number { return cfg.port + Number(list.length); }
const show = (): string => `${cfg.host}:${cfg.port}`;
const b = (n: number) => n * 10;
const reply = (s: string) => `<${s}>`;
const table = new Map<string, number>([["k", 1]]);
const cfg = { host: "h", port: 80 };
const list = [1, 2, 3];
let counter = 0;
const bump = () => { counter += step; return counter; };
const step = 5;
console.log(a(), handlers.ping(), handlers.count(), viaFn(), show(), bump(), bump());
