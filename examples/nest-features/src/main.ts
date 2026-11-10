import { ValidationPipe, VersioningType } from "@nestjs/common";
import { ConfigService } from "@nestjs/config";
import { NestFactory } from "@nestjs/core";
import { DocumentBuilder, SwaggerModule } from "@nestjs/swagger";
import compression from "compression";
import helmet from "helmet";
import { AppModule } from "./app.module";

async function bootstrap(): Promise<void> {
  const app = await NestFactory.create(AppModule, { bufferLogs: false });
  app.setGlobalPrefix("api");
  app.enableVersioning({ type: VersioningType.URI });
  app.use(helmet());
  app.use(compression());
  app.useGlobalPipes(new ValidationPipe());
  app.enableShutdownHooks();
  const config = new DocumentBuilder().setTitle("Nest features").setVersion("1.0").build();
  SwaggerModule.setup("docs", app, SwaggerModule.createDocument(app, config));
  const settings = app.get(ConfigService) as ConfigService;
  await app.listen(Number(settings.get<string>("PORT", "3100") ?? "3100"));
}
bootstrap();
