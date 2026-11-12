// exceljs (packages/exceljs): N rows of five cells written to a file. The peak memory must not grow with N
// (tests/exceljs/run.sh compares 100 000 and 1 000 000 rows). The writer is named as npm's exceljs names it:
// stream.xlsx.WorkbookWriter, in a type and after new.
import { stream } from "exceljs";

const out = process.argv[2];
const rows: int = Math.trunc(Number(process.argv[3]));
const wb: stream.xlsx.WorkbookWriter = new stream.xlsx.WorkbookWriter({ filename: out, useStyles: true });
const ws = wb.addWorksheet("Rows", { views: [{ state: "frozen", ySplit: 1 }] });
ws.columns = [
  { header: "Id", key: "id", width: 10 },
  { header: "Name", key: "name", width: 24 },
  { header: "Amount", key: "amount", width: 14, style: { numFmt: "#,##0.00" } },
  { header: "When", key: "when", width: 20, style: { numFmt: "dd/mm/yyyy hh:mm:ss" } },
  { header: "Flag", key: "flag", width: 8 },
];
ws.getRow(1).commit();
for (let i: int = 0; i < rows; i++) {
  ws.addRow([i, `name ${i % 1000}`, Number(i) * 1.25, new Date(Date.UTC(2026, 0, 1) + Number(i) * 1000), i % 2 === 0]).commit();
}
ws.commit();
await wb.commit();
console.log("rows", rows);
