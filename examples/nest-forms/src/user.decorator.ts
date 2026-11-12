import { createParamDecorator, ExecutionContext } from "@nestjs/common";

// the caller the guard put on the request, or one of its fields: @CurrentUser() user, @CurrentUser("sub") sub
export const CurrentUser = createParamDecorator((data: string | undefined, ctx: ExecutionContext) => {
  const request = ctx.switchToHttp().getRequest();
  const user = request.user;
  return data ? user?.[data] : user;
});
