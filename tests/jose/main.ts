// jose (v5), RS256: keys from PEM (PKCS#8 / SPKI), a generated key pair, exportJWK, SignJWT and jwtVerify with
// its claim checks and errors. RS256 signatures are deterministic: the tokens must be the same bytes as Node's.
import { exportJWK, exportSPKI, generateKeyPair, importPKCS8, importSPKI, jwtVerify, SignJWT } from "jose";
import type { JWK, JWTPayload, KeyLike } from "jose";
import { readFile } from "fs/promises";

const DIR = process.argv[2] ?? ".";
const ISS = "https://issuer.example";
const AUD = "kiosk-api";

interface Ctx {
  unit: string;
  seat: number;
}

async function verifyLine(name: string, token: string, key: KeyLike, iss: string, aud: string, algs: string[] = []): Promise<void> {
  try {
    const r = algs.length > 0 ? await jwtVerify(token, key, { issuer: iss, audience: aud, algorithms: algs }) : await jwtVerify(token, key, { issuer: iss, audience: aud });
    console.log(name, "ok", JSON.stringify(r.payload), JSON.stringify(r.protectedHeader));
  } catch (e) {
    const err = e as Error;
    const thrown: unknown = e;
    const code = (thrown as { code?: unknown }).code;
    console.log(name, "error", err.name, typeof code === "string" ? code : "", err.message);
  }
}

async function main(): Promise<void> {
  const privatePem = await readFile(`${DIR}/private.pem`, "utf-8");
  const publicPem = await readFile(`${DIR}/public.pem`, "utf-8");
  const priv = await importPKCS8(privatePem, "RS256");
  const pub = await importSPKI(publicPem, "RS256");
  console.log("types", priv.type, pub.type);

  // a token with fixed times: the same bytes on every run
  const ctx: Ctx = { unit: "A1", seat: 3 };
  const fixed = await new SignJWT({ tid: "KIOSK-01", sid: "s-1", cands: [10, 20], ctx: ctx })
    .setProtectedHeader({ alg: "RS256", kid: "k1", typ: "JWT" })
    .setIssuer(ISS)
    .setAudience(AUD)
    .setSubject("user-1")
    .setIssuedAt(1700000000)
    .setExpirationTime(4102444800)
    .setJti("jti-1")
    .sign(priv);
  console.log("token", fixed);
  await verifyLine("fixed", fixed, pub, ISS, AUD);
  await verifyLine("fixed algorithms", fixed, pub, ISS, AUD, ["RS256"]);
  const { payload, protectedHeader } = await jwtVerify(fixed, pub, { issuer: ISS, audience: AUD });
  const p: JWTPayload = payload;
  console.log("claims", p.sub, p.jti, p.exp, p.iat, p["tid"], typeof p.jti === "string" ? p.jti : null, protectedHeader.kid);

  // relative expiry, now
  const rel = await new SignJWT({ tid: "KIOSK-02" }).setProtectedHeader({ alg: "RS256", kid: "k1" }).setIssuer(ISS).setAudience(`${AUD}:advanced-operator`).setSubject("OPER").setIssuedAt().setExpirationTime("120s").sign(priv);
  const relp = (await jwtVerify(rel, pub, { issuer: ISS, audience: `${AUD}:advanced-operator`, algorithms: ["RS256"] })).payload;
  console.log("relative", (relp.exp as number) - (relp.iat as number), Math.abs((relp.iat as number) - Math.floor(Date.now() / 1000)) <= 2, relp.sub, relp["tid"]);
  const rel2 = await new SignJWT({}).setProtectedHeader({ alg: "RS256" }).setIssuedAt(1700000000).setExpirationTime("2h").sign(priv);
  const rel2p = (await jwtVerify(rel2, pub)).payload;
  console.log("2h", rel2p.iat, Math.abs((rel2p.exp as number) - Math.floor(Date.now() / 1000) - 7200) <= 2);

  // failures
  const expired = await new SignJWT({ tid: "x" }).setProtectedHeader({ alg: "RS256" }).setIssuer(ISS).setAudience(AUD).setIssuedAt(1700000000).setExpirationTime(1700000100).sign(priv);
  await verifyLine("expired", expired, pub, ISS, AUD);
  await verifyLine("issuer", fixed, pub, "https://other.example", AUD);
  await verifyLine("audience", fixed, pub, ISS, "other");
  const noAud = await new SignJWT({ tid: "x" }).setProtectedHeader({ alg: "RS256" }).setIssuer(ISS).setIssuedAt(1700000000).sign(priv);
  await verifyLine("no aud", noAud, pub, ISS, AUD);
  const notYet = await new SignJWT({}).setProtectedHeader({ alg: "RS256" }).setIssuer(ISS).setAudience(AUD).setNotBefore(4102444800).sign(priv);
  await verifyLine("nbf", notYet, pub, ISS, AUD);
  const parts = fixed.split(".");
  const sig = parts[2];
  const flipped = sig.slice(0, 10) + (sig.slice(10, 11) === "A" ? "B" : "A") + sig.slice(11);
  await verifyLine("signature", `${parts[0]}.${parts[1]}.${flipped}`, pub, ISS, AUD);
  await verifyLine("payload", `${parts[0]}.${parts[0]}.${sig}`, pub, ISS, AUD);
  await verifyLine("malformed", "abc.def", pub, ISS, AUD);
  await verifyLine("algorithm", fixed, pub, ISS, AUD, ["RS512"]);

  // JWK export
  const jwk = await exportJWK(pub);
  console.log("jwk", JSON.stringify(jwk));
  const base = await exportJWK(pub);
  const published: JWK = { ...base, kid: "k1", use: "sig", alg: "RS256" };
  console.log("published", JSON.stringify({ keys: [published] }));
  const pj = await exportJWK(priv);
  console.log("private jwk", pj.kty, pj.e, pj.n === jwk.n, (pj.d ?? "").length > 300, (pj.p ?? "").length, (pj.q ?? "").length, (pj.dp ?? "").length, (pj.dq ?? "").length, (pj.qi ?? "").length);

  // a generated key pair
  const kp = await generateKeyPair("RS256", { modulusLength: 2048 });
  const gj = await exportJWK(kp.publicKey);
  console.log("generated", kp.privateKey.type, kp.publicKey.type, gj.kty, gj.e, (gj.n ?? "").length);
  const gt = await new SignJWT({ tid: "gen" }).setProtectedHeader({ alg: "RS256", typ: "JWT" }).setIssuer(ISS).setAudience(AUD).setIssuedAt().setExpirationTime("1h").sign(kp.privateKey);
  const gp = (await jwtVerify(gt, kp.publicKey, { issuer: ISS, audience: AUD })).payload;
  console.log("generated verified", gp["tid"], (gp.exp as number) - (gp.iat as number));
  await verifyLine("generated with the fixed key", gt, pub, ISS, AUD);
  const gpem = await exportSPKI(kp.publicKey);
  const gpub = await importSPKI(gpem, "RS256");
  await verifyLine("generated through SPKI", fixed, gpub, ISS, AUD);
  console.log("spki pem", gpem.startsWith("-----BEGIN PUBLIC KEY-----\n"), gpem.endsWith("-----END PUBLIC KEY-----\n"));

  // bad PEM
  try {
    await importSPKI("not a pem", "RS256");
  } catch (e) {
    console.log("bad spki", (e as Error).name, (e as Error).message);
  }
  try {
    await importPKCS8(publicPem, "RS256");
  } catch (e) {
    console.log("bad pkcs8", (e as Error).name, (e as Error).message);
  }
}
main();
