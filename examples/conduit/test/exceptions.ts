// The built-in HTTP exceptions of @nestjs/common: run on Node (the real Nest) and on xisto, same output.
import { BadRequestException, ConflictException, HttpException, NotFoundException, UnauthorizedException } from "@nestjs/common";

const list: HttpException[] = [
  new NotFoundException(),
  new NotFoundException("User not found"),
  new BadRequestException("bad email", "Validation"),
  new UnauthorizedException(),
  new ConflictException("taken"),
  new HttpException({ errors: { username: "taken" } }, 422),
];
for (const ex of list) {
  console.log(ex.getStatus(), JSON.stringify(ex.getResponse()), ex instanceof NotFoundException, ex instanceof HttpException);
}
