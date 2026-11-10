import { createParamDecorator, ExecutionContext } from "@nestjs/common";
import { Request } from "express";

export const CurrentUser = createParamDecorator((data: string, ctx: ExecutionContext) => ctx.switchToHttp().getRequest<Request>().user);
