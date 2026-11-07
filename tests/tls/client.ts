const r: Response = await fetch("https://localhost:8443/hello?name=isa");
console.log(r.status, await r.text());
const b: Response = await fetch("https://localhost:8443/big");
console.log(b.status, (await b.text()).length);
