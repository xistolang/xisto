// a generated key pair: the private key (PKCS#8), the public key (SPKI) and a token signed with it, for openssl/Node
import { exportPKCS8, exportSPKI, generateKeyPair, SignJWT } from "jose";
import { writeFile } from "fs/promises";

async function main(): Promise<void> {
  const out = process.argv[2] ?? ".";
  const t0 = Date.now();
  const kp = await generateKeyPair("RS256", { modulusLength: 2048 });
  const ms = Date.now() - t0;
  await writeFile(`${out}/gen-private.pem`, await exportPKCS8(kp.privateKey));
  await writeFile(`${out}/gen-public.pem`, await exportSPKI(kp.publicKey));
  const token = await new SignJWT({ tid: "gen" }).setProtectedHeader({ alg: "RS256" }).setIssuer("i").setAudience("a").setIssuedAt(1700000000).setExpirationTime(4102444800).sign(kp.privateKey);
  await writeFile(`${out}/gen-token.txt`, token);
  console.log("generated in", ms < 5000 ? "under 5 s" : `${ms} ms`);
}
main();
