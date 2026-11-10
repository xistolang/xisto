import { ArgumentsHost, BadRequestException, Catch, ExceptionFilter } from "@nestjs/common";
import { Response } from "express";

@Catch(BadRequestException)
export class TeapotFilter implements ExceptionFilter {
  catch(exception: BadRequestException, host: ArgumentsHost): void {
    const res = host.switchToHttp().getResponse<Response>();
    res.status(418).json({ teapot: true, status: exception.getStatus() });
  }
}
