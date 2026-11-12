import { Module, VersioningType } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";
import { MultiController, NeutralController, TwoController } from "./versions.controller";

@Module({ controllers: [MultiController, NeutralController, TwoController] })
class AppModule {}

async function main(): Promise<void> {
  const app = await NestFactory.create(AppModule, { logger: false });
  app.setGlobalPrefix("api");
  app.enableVersioning({ type: VersioningType.URI });
  await app.listen(Number(process.env.PORT ?? "3199"));
}
main();
