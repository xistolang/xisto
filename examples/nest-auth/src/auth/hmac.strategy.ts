import { Injectable } from "@nestjs/common";
import { PassportStrategy } from "@nestjs/passport";
import { Request } from "express";
import { ExtractJwt, Strategy } from "passport-jwt";
import { AuthUser, TokenPayload } from "./user";

// HS256 tokens of the legacy login, with the request at hand
@Injectable()
export class HmacStrategy extends PassportStrategy(Strategy, "hmac") {
  constructor() {
    super({
      jwtFromRequest: ExtractJwt.fromAuthHeaderAsBearerToken(),
      secretOrKey: "test-secret",
      algorithms: ["HS256"],
      passReqToCallback: true,
    });
  }

  validate(req: Request, payload: TokenPayload): AuthUser | null {
    if (payload.name.length === 0) {
      return null;
    }
    return new AuthUser(payload.sub, payload.name + " (" + req.method + ")", payload.roles, "hmac");
  }
}
