// TypeScript as people write it: object types inside type arguments, an inline `type` modifier on an import,
// parameters typed by their literal default (also in arrows), static fields typed by their value.
import { type Point, midpoint } from "./lib/geometry.ts";
import { Point as P } from "./lib/geometry.ts";

class Store {
  private static readonly NAMES = ["ana", "bia"];
  private static readonly CODE = /^[A-Z]{2}\d+$/;
  static readonly LIMIT = 3;

  async query<T = object>(sql: string, params: unknown[] = []): Promise<T> {
    console.log(sql.length > 10, params.length);
    const raw: object = JSON.parse('[{"IDADE": 42, "NOME": "ana"}]');
    const rows: T = raw;
    return rows;
  }

  page(n: number, size = 20, prefix = "p", desc = false): string {
    return `${prefix}${n * size}${desc ? "-" : "+"}`;
  }

  valid(code: string): boolean {
    return Store.CODE.test(code) && Store.NAMES.length > 1 && Store.LIMIT > 2;
  }
}

async function main(): Promise<void> {
  const s = new Store();
  const rows = await s.query<Array<{ IDADE: number | null; NOME: string }>>(
    `SELECT FN_IDADE(TO_DATE(:1, 'YYYY-MM-DD'), TO_DATE(:2, 'YYYY-MM-DD')) AS IDADE FROM DUAL`,
    ["2020-01-01", "2024-01-01"],
  );
  console.log(rows.length, rows[0].NOME);
  const multi = await s.query<{
    IDADE: number;
    NOME: string | null;
  }[]>(`SELECT 'a' AS NOME, 1 AS IDADE FROM DUAL WHERE X = ${rows.length}`);
  console.log(multi[0].IDADE, multi[0].NOME);
  console.log(s.page(2), s.page(2, 5), s.page(2, 5, "q", true));
  console.log(s.valid("AB12"), s.valid("x"));
  const scale = (n: number, by = 2, unit = "px"): string => `${n * by}${unit}`;
  console.log(scale(7), scale(7, 3), scale(7, 3, "em"));
  const m: Point = midpoint(new P(0, 0), new P(4, 2));
  console.log(m.x, m.y);
  const a = 1, b = 2, c = 3;
  console.log(a < b && c > a);
}
main();
