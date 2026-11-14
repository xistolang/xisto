// TechEmpower Framework Benchmarks: NestJS on xisto, phase 1 (/plaintext and /json).
// The controller follows the TFB's own frameworks/TypeScript/nest (src/sql/sql.controller.ts): @Header('Server'),
// the JSON returned as an object and serialized by Nest per request. The TFB app forks one Node worker per CPU with
// node:cluster; here XISTO_WORKERS sets the event loops instead (node:cluster is not available in xisto).
// The database routes (db, queries, fortunes, updates with TypeORM) come in phase 2.
// Build: cd bench/techempower/xisto/nest && xisto install && xisto build   (PORT, default 9101)
import { Controller, Get, Header, Module, Res } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { Request, Response, NextFunction } from 'express';

interface Message {
  message: string;
}

@Controller()
class AppController {
  @Get('/json')
  @Header('Server', 'NestJS')
  getJson(): Message {
    return { message: 'Hello, World!' };
  }

  // (gap: @Header('Content-Type') is added after the default text/html instead of replacing it, so the reply
  // carries two Content-Type lines; the closest form sets it on the response)
  @Get('plaintext')
  @Header('Server', 'NestJS')
  getHello(@Res() res: Response): void {
    res.type('text/plain').send('Hello, World!');
  }
}

@Module({ controllers: [AppController] })
class AppModule {}

async function bootstrap(): Promise<void> {
  const port: number = Number(process.env.PORT ?? '9101');
  const app = await NestFactory.create(AppModule, { logger: false });
  // (gap: Node's http adds Date to every response; xisto's server does not, so a middleware sets it)
  app.use((req: Request, res: Response, next: NextFunction): void => {
    res.setHeader('Date', new Date().toUTCString());
    next();
  });
  await app.listen(port);
}
bootstrap();
