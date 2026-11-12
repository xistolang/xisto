import { registerAs } from "@nestjs/config";

export interface FormsConfig {
  issuer: string;
  ttlSec: number;
  kid?: string;
}

function parseTtl(raw: string | undefined): number {
  const parsed = parseInt(raw ?? "3600", 10);
  if (Number.isNaN(parsed) || parsed < 60) return 3600;
  return parsed;
}

export default registerAs<FormsConfig>("forms", () => ({
  issuer: process.env.FORMS_ISSUER ?? "forms-app",
  ttlSec: parseTtl(process.env.FORMS_TTL_SEC),
  kid: process.env.FORMS_KID || undefined,
}));
