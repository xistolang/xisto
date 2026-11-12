import { Inject, Injectable } from "@nestjs/common";
import { ConfigType } from "@nestjs/config";
import formsConfig from "./forms.config";

@Injectable()
export class FormsService {
  constructor(
    @Inject(formsConfig.KEY)
    private readonly cfg: ConfigType<typeof formsConfig>,
  ) {}

  settings(): { issuer: string; ttlSec: number; hasKid: boolean } {
    return { issuer: this.cfg.issuer, ttlSec: this.cfg.ttlSec, hasKid: this.cfg.kid !== undefined };
  }
}
