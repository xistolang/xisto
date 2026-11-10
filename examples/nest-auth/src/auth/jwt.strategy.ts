import { ForbiddenException, Injectable } from "@nestjs/common";
import { ConfigService } from "@nestjs/config";
import { PassportStrategy } from "@nestjs/passport";
import { passportJwtSecret } from "jwks-rsa";
import { ExtractJwt, Strategy } from "passport-jwt";
import { AuthUser, TokenPayload } from "./user";

// RS256 tokens of the identity provider, checked with the keys of its JWKS (served over HTTP)
@Injectable()
export class JwtStrategy extends PassportStrategy(Strategy, "jwt") {
  constructor(config: ConfigService) {
    super({
      jwtFromRequest: ExtractJwt.fromAuthHeaderAsBearerToken(),
      secretOrKeyProvider: passportJwtSecret({
        cache: true,
        rateLimit: true,
        jwksRequestsPerMinute: 10,
        jwksUri: config.get<string>("JWKS_URI", "http://127.0.0.1:3199/certs") ?? "",
      }),
      issuer: "https://idp.test",
      algorithms: ["RS256"],
      ignoreExpiration: false,
    });
  }

  validate(payload: TokenPayload): AuthUser {
    if (payload.sub === "blocked") {
      throw new ForbiddenException("user is blocked");
    }
    return new AuthUser(payload.sub, payload.name, payload.roles, "jwks");
  }
}
