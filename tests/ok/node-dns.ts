// node:dns: lookup of localhost and of a name that does not exist, with callbacks and promises (same output as Node)
import dns from "node:dns";

async function viaPromises(): Promise<void> {
  const a = await dns.promises.lookup("127.0.0.1");
  console.log("promises:", a.address, a.family);
  try {
    await dns.promises.lookup("no-such-host.invalid");
  } catch (err) {
    console.log("rejected:", (err as Error).message);
  }
}

dns.lookup("localhost", (err, address, family) => {
  console.log("lookup:", err === null, address === "127.0.0.1" || address === "::1", family === 4 || family === 6);
  dns.lookup("no-such-host.invalid", (err2) => {
    console.log("missing:", err2 !== null ? err2.message : "none");
    viaPromises();
  });
});
