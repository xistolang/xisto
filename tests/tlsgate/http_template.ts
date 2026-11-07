const host = "127.0.0.1";
const r = await fetch(`http://${host}:1/x`);
console.log(r.status);
