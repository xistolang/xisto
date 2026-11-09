// exceljs (packages/exceljs): a report written as a stream, the way a reconciliation export writes one:
// a summary sheet of label/value rows, a list with a header row, frozen panes, an autoFilter, number and date
// formats, a bold header, escaped text and empty cells. Written to a file and to a memory sink (same bytes).
import { WorkbookWriter, MemorySink, Worksheet } from "exceljs";

const MONEY = "#,##0.00";
const DATE_TIME = "dd/mm/yyyy hh:mm:ss";

function sheetWithColumns(wb: WorkbookWriter, name: string): Worksheet {
  const ws = wb.addWorksheet(name, { views: [{ state: "frozen", ySplit: 1 }] });
  ws.columns = [
    { header: "Code", key: "code", width: 18 },
    { header: "Company", key: "company", width: 30 },
    { header: "Amount", key: "amount", width: 14, style: { numFmt: MONEY } },
    { header: "When", key: "when", width: 20, style: { numFmt: DATE_TIME } },
    { header: "", key: "note", width: 40 },
  ];
  ws.getRow(1).font = { bold: true };
  ws.getRow(1).commit();
  ws.autoFilter = { from: { row: 1, column: 1 }, to: { row: 1, column: 5 } };
  return ws;
}

async function build(wb: WorkbookWriter, rows: int): Promise<void> {
  wb.creator = "report-test";
  wb.created = new Date(Date.UTC(2026, 10, 10, 12, 0, 0));
  wb.modified = wb.created;

  const summary = wb.addWorksheet("Summary");
  summary.columns = [
    { header: "Field", key: "field", width: 40 },
    { header: "Value", key: "value", width: 30 },
  ];
  summary.getRow(1).font = { bold: true };
  summary.getRow(1).commit();
  const line = (field: string, value: unknown, numFmt: string): void => {
    const r = summary.addRowByKey(["field", "value"], [field, value]);
    if (numFmt !== "") r.getCell("value").numFmt = numFmt;
    r.getCell("value").alignment = { horizontal: "left" };
    r.commit();
  };
  line("Companies", 42, "");
  line("Total", 1234567.891, MONEY);
  line("First run", new Date(Date.UTC(2026, 0, 31, 23, 59, 58)), DATE_TIME);
  line("Note with <xml> & \"quotes\"", "a < b & c > d", "");
  line("Missing", null, "");
  line("Ok?", true, "");
  summary.commit();

  const ws = sheetWithColumns(wb, "Orders");
  for (let i: int = 0; i < rows; i++) {
    const r = ws.addRow([
      `C${i}`,
      i % 7 === 0 ? "Ação & Cia" : `Company ${i % 97}`,
      (i * 37) % 100000 / 100,
      new Date(Date.UTC(2026, 0, 1) + Number(i) * 60000),
      i % 5 === 0 ? null : "x",
    ]);
    if (i % 1000 === 0) r.getCell(3).font = { bold: true };
    r.commit();
  }
  ws.commit();
  await wb.commit();
}

const args = process.argv;
const rows: int = args.length > 3 ? Math.trunc(Number(args[3])) : 1000;
const out = args.length > 2 ? args[2] : "report.xlsx";
await build(new WorkbookWriter({ filename: out, useStyles: true }), rows);
const sink = new MemorySink();
await build(new WorkbookWriter({ stream: sink, useStyles: true }), rows);
const bytes = sink.toBuffer();
console.log("file written; stream bytes", bytes.length, "ended", sink.ended);
