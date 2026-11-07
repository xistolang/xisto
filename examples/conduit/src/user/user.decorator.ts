import { createParamDecorator, ExecutionContext } from "@nestjs/common";
import { Request } from "express";
import { readToken } from "../shared/token";

// The original @User('id') / @User('email') read a field of a dynamic object; typed, they are two decorators.
// Protected routes have a valid token (AuthGuard); elsewhere the user is optional: 0 and "" when absent.
export const UserId = createParamDecorator((data: string, ctx: ExecutionContext): int => {
  const req: Request = ctx.switchToHttp().getRequest();
  const auth = req.headers.authorization;
  if (!auth) {
    return 0;
  }
  const payload = readToken(auth);
  if (payload === null) {
    return 0;
  }
  return payload.id;
});

export const UserEmail = createParamDecorator((data: string, ctx: ExecutionContext): string => {
  const req: Request = ctx.switchToHttp().getRequest();
  const auth = req.headers.authorization;
  if (!auth) {
    return "";
  }
  const payload = readToken(auth);
  if (payload === null) {
    return "";
  }
  return payload.email;
});
