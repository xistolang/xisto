// the base's method fetches inside a closure; only a subclass is made: TLS
class Base {
  url: string = "";
  send(): void {
    const go = async (): Promise<void> => { const r = await fetch(this.url); console.log(r.status); };
    if (this.url.length > 100) go();
  }
}
class Sub extends Base {
  name: string = "sub";
}
new Sub().send();
console.log("ok");
