import { Controller, Get, Header } from "@nestjs/common";

export class Health {
  status: string;
}

@Controller("health")
export class HealthController {
  @Get()
  @Header("Cache-Control", "no-store")
  check(): Health {
    return { status: "ok" };
  }
}
