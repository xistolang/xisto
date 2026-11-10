import { Module } from "@nestjs/common";
import { ConfigModule } from "@nestjs/config";
import { APP_GUARD, APP_INTERCEPTOR } from "@nestjs/core";
import { PassportModule } from "@nestjs/passport";
import { ThrottlerGuard, ThrottlerModule } from "@nestjs/throttler";
import { HmacStrategy } from "./auth/hmac.strategy";
import { JwtAuthGuard } from "./auth/jwt-auth.guard";
import { JwtStrategy } from "./auth/jwt.strategy";
import { RolesGuard } from "./auth/roles.guard";
import { AuditLog } from "./common/audit-log";
import { LoggingInterceptor } from "./common/logging.interceptor";
import { TransformInterceptor } from "./common/transform.interceptor";
import { AdminController } from "./items/admin.controller";
import { HealthController } from "./items/health.controller";
import { ItemsController } from "./items/items.controller";

@Module({
  imports: [
    ConfigModule.forRoot({ isGlobal: true }),
    PassportModule.register({ defaultStrategy: "jwt" }),
    ThrottlerModule.forRoot([
      { name: "short", ttl: 1000, limit: 50 },
      { ttl: 60000, limit: 100 },
    ]),
  ],
  controllers: [HealthController, ItemsController, AdminController],
  providers: [
    AuditLog,
    JwtStrategy,
    HmacStrategy,
    RolesGuard,
    { provide: APP_GUARD, useClass: JwtAuthGuard },
    { provide: APP_GUARD, useClass: ThrottlerGuard },
    { provide: APP_INTERCEPTOR, useClass: LoggingInterceptor },
    { provide: APP_INTERCEPTOR, useClass: TransformInterceptor },
  ],
})
export class AppModule {}
