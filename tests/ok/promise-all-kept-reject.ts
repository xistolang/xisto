// XB-204: Promise.all / allSettled of an array of promises kept in a variable, where a later one rejects while an
// earlier one is still pending: the rejection is Promise.all's (caught by the try), not an uncaught one; Promise.all
// rejects as soon as the first one does (in time, not in index order); the values come in the array's order
import { setTimeout as sleep } from "node:timers/promises";
async function work(ms: int, v: string): Promise<string> { await sleep(ms); console.log("done", v); return v; }
async function fail(ms: int, msg: string): Promise<string> { await sleep(ms); throw new Error(msg); }
async function tick(ms: int): Promise<void> { await sleep(ms); console.log("tick", ms); }
async function main(): Promise<void> {
    const ps: Promise<string>[] = [work(40, "slow"), fail(1, "first")];
    try { await Promise.all(ps); console.log("not here"); } catch (e) { console.log("caught", (e as Error).message); }
    await sleep(60);
    const both: Promise<string>[] = [fail(30, "late"), fail(5, "early")];
    try { await Promise.all(both); } catch (e) { console.log("caught", (e as Error).message); }
    await sleep(40);
    const ok: Promise<string>[] = [work(20, "a"), work(1, "b"), work(10, "c")];
    const vs = await Promise.all(ok);
    console.log(vs.join(","));
    const none: Promise<string>[] = [];
    console.log((await Promise.all(none)).length);
    const vp: Promise<void>[] = [tick(15), tick(2)];
    await Promise.all(vp);
    const ss: Promise<string>[] = [work(20, "s1"), fail(1, "s2")];
    const rs = await Promise.allSettled(ss);
    for (const r of rs) console.log(r.status);
    const sv: Promise<void>[] = [tick(10), (async (): Promise<void> => { await sleep(1); throw new Error("v"); })()];
    const rv = await Promise.allSettled(sv);
    for (const r of rv) console.log(r.status);
}
main();
