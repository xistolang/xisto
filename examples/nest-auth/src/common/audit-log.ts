import { Injectable } from "@nestjs/common";

// what the interceptors saw, in order (GET /api/audit shows it)
@Injectable()
export class AuditLog {
  readonly lines: string[] = [];

  add(line: string): void {
    this.lines.push(line);
  }
}
