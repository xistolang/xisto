// XB-162: a generic method races a stored promise against a timer kept in a `let`, cleared in the `then`: the program
// ends as soon as nothing is left (a cleared timer's promise, never settled, keeps nothing alive), with nothing leaked;
// clearTimeout of a timer that may be undefined (ReturnType<typeof setTimeout> | undefined) does nothing then, as Node
class Guard {
    async withTimeout<T>(p: Promise<T>, ms: number, fallback: T): Promise<T> {
        let timer: ReturnType<typeof setTimeout> | undefined;
        const wait = new Promise<T>((resolve) => {
            timer = setTimeout(() => resolve(fallback), ms);
        });
        return Promise.race([p, wait]).then((v) => {
            clearTimeout(timer);
            return v;
        });
    }
}
class Svc {
    private inflight: Promise<number> | null = null;
    private count = 0;
    private async fetchCount(): Promise<number> {
        await new Promise<void>((r) => { setTimeout(r, 5); });
        this.count++;
        return this.count;
    }
    race<T>(p: Promise<T>, ms: number, fb: T): Promise<T> {
        let timer = 0;
        const wait = new Promise<T>((resolve) => { timer = setTimeout(() => resolve(fb), ms); });
        return Promise.race([p, wait]).then((v) => { clearTimeout(timer); return v; });
    }
    get(): Promise<number> {
        let stored = this.inflight;
        if (stored === null) { stored = this.fetchCount(); this.inflight = stored; }
        return this.race(stored, 20000, -1);
    }
}
async function slow(ms: number, v: string): Promise<string> {
    await new Promise<void>((r) => { setTimeout(r, ms); });
    return v;
}
async function main(): Promise<void> {
    const t0 = Date.now();
    const g = new Guard();
    const stored = slow(5, "fast enough");
    console.log(await g.withTimeout(stored, 20000, "timed out"));
    const late = slow(60, "late");
    console.log(await g.withTimeout(late, 10, "timed out"));
    const none: ReturnType<typeof setTimeout> | undefined = undefined;
    clearTimeout(none);
    const s = new Svc();
    s.get().then((v) => console.log("got", v));
    console.log("got again", await s.get());
    console.log("quick exit", Date.now() - t0 < 5000);
}
main();
