// fetch(url, { options }) kept as a promise (.then/.catch, not awaited): the options are evaluated when it starts
const ac = new AbortController(); ac.abort();
fetch("http://127.0.0.1:9/", { signal: ac.signal }).catch((e: Error) => console.log(e.name));
fetch("http://127.0.0.1:9/", { method: "POST", body: "x", headers: { "X-A": "b" + "c" }, signal: ac.signal }).then((r) => console.log(r.status)).catch((e: Error) => console.log("2", e.name));
