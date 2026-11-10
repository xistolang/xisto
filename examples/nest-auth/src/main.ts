import { ConfigService } from "@nestjs/config";
import { NestFactory } from "@nestjs/core";
import { AppModule } from "./app.module";

async function bootstrap(): Promise<void> {
  const app = await NestFactory.create(AppModule);
  app.setGlobalPrefix("api");
  const settings = app.get(ConfigService) as ConfigService;
  await app.listen(Number(settings.get<string>("PORT", "3198") ?? "3198"));
}
bootstrap();
