// @nestjs/jwt's JwtService: sign with expiresIn/notBefore/issuer from the module and per call, verify (expired,
// bad signature, issuer, ignoreExpiration), decode. iat is given so the tokens do not depend on the clock.
import { JwtService } from "@nestjs/jwt";

const jwt = new JwtService({ secret: "module-secret", signOptions: { expiresIn: "15m", issuer: "core" } });
const t1 = jwt.sign({ sub: "ANA", usuario: "ANA", cdEmpresaPlano: 7, iat: 1700000000 });
console.log(t1);
const t2 = jwt.sign({ sub: "BIA", iat: 1700000000 }, { secret: "refresh", expiresIn: "7d" });
console.log(t2);
const t3 = jwt.sign({ name: "CAIO", iat: 1700000000 }, { expiresIn: 3600, notBefore: "1h", subject: "x" });
console.log(t3);
for (const [token, secret] of [[t1, "module-secret"], [t1, "wrong"], [t2, "refresh"], ["a.b", "s"]]) {
  try {
    jwt.verify(token, { secret });
    console.log("valid");
  } catch (e) {
    console.log((e as Error).name, (e as Error).message);
  }
}
const p = jwt.verify(t1, { secret: "module-secret", ignoreExpiration: true });
console.log(p.sub, p.cdEmpresaPlano, p.exp, p.iss);
try {
  jwt.verify(t1, { secret: "module-secret", ignoreExpiration: true, issuer: "other" });
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
const d = jwt.decode(t2);
console.log(d.sub, d.exp, jwt.decode("nope"));
