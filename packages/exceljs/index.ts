// exceljs (contract ^4): the streaming workbook writer (stream.xlsx.WorkbookWriter) for xisto.
//
// A workbook written row by row in constant memory: each committed row goes straight into the deflated ZIP entry
// of its worksheet (native/xlsx.c) and is then forgotten, and strings are written inline in the cell (no shared
// strings table to keep). The bytes go to a file (options.filename) or to a sink (options.stream).
//
// Compatibility with npm's exceljs:
// - `import { stream } from "exceljs"` and `stream.xlsx.WorkbookWriter` (in a type and after new) name the
//   WorkbookWriter below, as in exceljs; it can also be imported directly.
// - addRow takes an array of values (strings, numbers, booleans, Dates, null) or an object by column key
//   (`addRow({ key: value })`, as exceljs).
// - Styles cover what reports use: numFmt, font (bold, italic, size, name), alignment (horizontal, vertical,
//   wrapText), column widths, frozen panes (views) and autoFilter.
import { Writable } from "node:stream";

declare class NativeZip {
  constructor(path: string);
  open(name: string): void;
  text(s: string): void;
  row(r: int, values: unknown[], styles: int[], dateStyle: int, rowStyle: int): void;
  close(): void;
  finish(): void;
  take(): Uint8Array;
  pending(): int;
}

/** Where a WorkbookWriter sends its bytes when it is not writing a file: write(chunk) for each piece, end() once. */
export abstract class WritableSink {
  abstract write(chunk: Uint8Array): void;
  abstract end(): void;
}

/** A Node stream (a PassThrough, a file stream) given as `stream`: each piece is written to it, end() at the end. */
class NodeStreamSink extends WritableSink {
  constructor(private readonly w: Writable) {
    super();
  }
  write(chunk: Uint8Array): void {
    this.w.write(chunk);
  }
  end(): void {
    this.w.end();
  }
}

/** A sink that keeps the bytes in memory: toBuffer() gives the whole workbook once it is committed. */
export class MemorySink extends WritableSink {
  private readonly parts: Uint8Array[] = [];
  private size: int = 0;
  ended: boolean = false;
  write(chunk: Uint8Array): void {
    this.parts.push(chunk);
    this.size += chunk.length;
  }
  end(): void {
    this.ended = true;
  }
  /** The bytes written so far, as one Buffer. */
  toBuffer(): Buffer {
    const out = Buffer.alloc(this.size);
    let at: int = 0;
    for (const p of this.parts) {
      out.set(p, at);
      at += p.length;
    }
    return out;
  }
}

export interface WorkbookWriterOptions {
  /** Write the workbook to this file. */
  filename?: string;
  /** Or send the bytes to this sink or Node stream (write for each piece, end at the end). */
  stream?: WritableSink | Writable;
  /** Write the styles (number formats, fonts, alignment); without it every cell is unstyled, as in exceljs. */
  useStyles?: boolean;
  /** Accepted for compatibility: strings are always written inline in their cells, so memory stays constant. */
  useSharedStrings?: boolean;
}

export interface Font {
  bold?: boolean;
  italic?: boolean;
  size?: number;
  name?: string;
}
export interface Alignment {
  horizontal?: string;
  vertical?: string;
  wrapText?: boolean;
}
export interface Style {
  numFmt?: string;
  font?: Font;
  alignment?: Alignment;
}
export interface Column {
  header?: string;
  key?: string;
  width?: number;
  style?: Style;
}
export interface WorksheetView {
  state?: string;
  xSplit?: number;
  ySplit?: number;
}
export interface AddWorksheetOptions {
  views?: WorksheetView[];
}
export interface CellAddress {
  row: number;
  column: number;
}
export interface AutoFilter {
  from: CellAddress;
  to: CellAddress;
}

/** One cell of a row that is not committed yet: its style can still change. */
export class Cell {
  numFmt: string = "";
  font: Font | null = null;
  alignment: Alignment | null = null;
}

/** A row of a worksheet. Change its cells and styles, then commit() it: from then on it is written and gone. */
export class Row {
  readonly number: int;
  values: unknown[];
  font: Font | null = null;
  numFmt: string = "";
  alignment: Alignment | null = null;
  committed: boolean = false;
  readonly cells: Map<int, Cell> = new Map<int, Cell>();
  private readonly sheet: Worksheet;

  constructor(sheet: Worksheet, number: int, values: unknown[]) {
    this.sheet = sheet;
    this.number = number;
    this.values = values;
  }

  /** The cell at a column: its 1-based number, or the key given in ws.columns. */
  getCell(column: string | number): Cell {
    const i: int = this.sheet.columnIndex(column);
    if (this.cells.has(i)) return this.cells.get(i)!;
    const c = new Cell();
    this.cells.set(i, c);
    return c;
  }

  /** Write this row (and every row before it that is still open) to the workbook. */
  commit(): void {
    this.sheet.commitThrough(this.number);
  }
}

/** A worksheet of a WorkbookWriter: rows are added in order and written as they are committed. */
export class Worksheet {
  readonly name: string;
  readonly id: int;
  autoFilter: AutoFilter | null = null;
  private cols: Column[] = [];
  private readonly views: WorksheetView[];
  private readonly book: WorkbookWriter;
  private readonly open: Row[] = [];
  private next: int = 1;
  started: boolean = false;
  done: boolean = false;

  constructor(book: WorkbookWriter, id: int, name: string, views: WorksheetView[]) {
    this.book = book;
    this.id = id;
    this.name = name;
    this.views = views;
  }

  /** The columns: header (written as row 1 when any column has one), key, width and style. */
  get columns(): Column[] {
    return this.cols;
  }
  set columns(cols: Column[]) {
    if (this.next > 1) throw new Error("exceljs: set the columns before adding rows");
    this.cols = cols;
    let any = false;
    for (const c of cols) if ((c.header ?? "") !== "") any = true;
    if (any) {
      const headers: unknown[] = [];
      for (const c of cols) {
        const h: string = c.header ?? "";
        if (h === "") headers.push(null);
        else headers.push(h);
      }
      this.addRow(headers);
    }
  }

  /** The 0-based index of a column given by its 1-based number or its key. */
  columnIndex(column: string | number): int {
    if (typeof column === "number") {
      if (column < 1) throw new Error(`exceljs: no column ${column}`);
      return Math.trunc(column) - 1;
    }
    for (let i: int = 0; i < this.cols.length; i++) {
      if ((this.cols[i].key ?? "") === column) return i;
    }
    throw new Error(`exceljs: no column with key '${column}'`);
  }

  /** Add a row of values in column order: strings, numbers, booleans, Dates or null (an empty cell). */
  addRow(values: Record<string, unknown> | unknown[]): Row {
    if (!Array.isArray(values)) {
      return this.addRowByKey(Object.keys(values), Object.values(values)); // addRow({ key: value }): by column key
    }
    if (this.done) throw new Error(`exceljs: worksheet '${this.name}' is already committed`);
    const r = new Row(this, this.next, values);
    this.next++;
    this.open.push(r);
    return r;
  }

  /** Add a row from keyed values: keys[i] is the column key of values[i] (the object form of exceljs). */
  addRowByKey(keys: string[], values: unknown[]): Row {
    const row: unknown[] = [];
    for (let i: int = 0; i < this.cols.length; i++) row.push(null);
    for (let k: int = 0; k < keys.length && k < values.length; k++) {
      const i: int = this.columnIndex(keys[k]);
      while (row.length <= i) row.push(null);
      row[i] = values[k];
    }
    return this.addRow(row);
  }

  /** A row that is not committed yet, by its 1-based number. */
  getRow(n: int): Row {
    for (const r of this.open) if (r.number === n) return r;
    if (n >= this.next) {
      const none: unknown[] = [];
      while (this.next <= n) this.addRow(none);
      return this.getRow(n);
    }
    throw new Error(`exceljs: row ${n} is already committed`);
  }

  /** Write every open row up to number n. */
  commitThrough(n: int): void {
    if (!this.book.claim(this)) {
      for (const r of this.open) if (r.number <= n) r.committed = true;
      return;
    }
    this.flush(n);
  }

  /** Write the rows still open and close the worksheet. */
  commit(): void {
    if (this.done) return;
    for (const r of this.open) r.committed = true;
    this.done = true;
    this.book.sheetDone(this);
  }

  private flush(n: int): void {
    const zip = this.book.zip;
    if (!this.started) {
      this.started = true;
      zip.open(`xl/worksheets/sheet${this.id}.xml`);
      zip.text(this.head());
    }
    while (this.open.length > 0 && (this.open[0].number <= n || this.open[0].committed)) {
      const r = this.open.shift()!;
      this.writeRow(r);
    }
    this.book.drain();
  }

  /** (the workbook's turn has come: write what is committed, and close when the sheet is done) */
  resume(): void {
    this.flush(0);
    if (this.done) this.finish();
  }

  finish(): void {
    const zip = this.book.zip;
    if (!this.started) {
      this.started = true;
      zip.open(`xl/worksheets/sheet${this.id}.xml`);
      zip.text(this.head());
    }
    while (this.open.length > 0) this.writeRow(this.open.shift()!);
    let tail = "</sheetData>";
    const af = this.autoFilter;
    if (af !== null) tail += `<autoFilter ref="${this.filterRef()}"/>`;
    tail += "</worksheet>";
    zip.text(tail);
    zip.close();
    this.book.drain();
  }

  /** The autoFilter range with absolute references ($A$1:$H$1), as a defined name needs it. */
  absoluteFilterRef(): string {
    const af = this.autoFilter!;
    return `$${colName(Math.trunc(af.from.column) - 1)}$${af.from.row}:$${colName(Math.trunc(af.to.column) - 1)}$${af.to.row}`;
  }

  filterRef(): string {
    const af = this.autoFilter!;
    return `${colName(Math.trunc(af.from.column) - 1)}${af.from.row}:${colName(Math.trunc(af.to.column) - 1)}${af.to.row}`;
  }

  private writeRow(r: Row): void {
    const styles: int[] = [];
    const n: int = r.values.length;
    let rowStyle: int = 0;
    if (this.book.styled) {
      if (r.font !== null || r.numFmt !== "" || r.alignment !== null) rowStyle = this.book.styleId(r.numFmt, r.font, r.alignment);
      for (let i: int = 0; i < n; i++) {
        const col: Style | null = i < this.cols.length ? columnStyle(this.cols[i]) : null;
        const cell: Cell | null = r.cells.has(i) ? r.cells.get(i)! : null;
        let numFmt: string = cell !== null ? cell.numFmt : "";
        if (numFmt === "") numFmt = r.numFmt;
        if (numFmt === "" && col !== null) numFmt = col.numFmt ?? "";
        let font: Font | null = cell !== null ? cell.font : null;
        if (font === null) font = r.font;
        if (font === null && col !== null) { const cf = col.font; if (cf != null) font = cf; }
        let al: Alignment | null = cell !== null ? cell.alignment : null;
        if (al === null) al = r.alignment;
        if (al === null && col !== null) { const ca = col.alignment; if (ca != null) al = ca; }
        styles.push(numFmt === "" && font === null && al === null ? 0 : this.book.styleId(numFmt, font, al));
      }
    }
    this.book.zip.row(r.number, r.values, styles, this.book.styled ? this.book.dateStyle : 0, rowStyle);
  }

  private head(): string {
    let s = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
      + 'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">';
    let pane = "";
    for (const v of this.views) {
      if ((v.state ?? "") === "frozen") {
        const xs: int = Math.trunc(v.xSplit ?? 0);
        const ys: int = Math.trunc(v.ySplit ?? 0);
        if (xs > 0 || ys > 0) {
          const active = xs > 0 && ys > 0 ? "bottomRight" : ys > 0 ? "bottomLeft" : "topRight";
          pane = `<pane${xs > 0 ? ` xSplit="${xs}"` : ""}${ys > 0 ? ` ySplit="${ys}"` : ""} topLeftCell="${colName(xs)}${ys + 1}" activePane="${active}" state="frozen"/>`;
        }
      }
    }
    s += `<sheetViews><sheetView workbookViewId="0"${this.id === 1 ? ' tabSelected="1"' : ""}>${pane}</sheetView></sheetViews>`;
    s += '<sheetFormatPr defaultRowHeight="15"/>';
    let cols = "";
    for (let i: int = 0; i < this.cols.length; i++) {
      const w: number = this.cols[i].width ?? -1;
      if (w >= 0) cols += `<col min="${i + 1}" max="${i + 1}" width="${w}" customWidth="1"/>`;
    }
    if (cols !== "") s += `<cols>${cols}</cols>`;
    return s + "<sheetData>";
  }
}

/** The style of a column, or null when it has none. */
function columnStyle(c: Column): Style | null {
  const st = c.style;
  if (st == null) return null;
  return st;
}

/** The column letters of a 0-based index: 0 -> A, 25 -> Z, 26 -> AA. */
function colName(i: int): string {
  let n: int = i + 1;
  let s = "";
  while (n > 0) {
    const r: int = (n - 1) % 26;
    s = String.fromCharCode(65 + r) + s;
    n = Math.trunc((n - 1) / 26);
  }
  return s;
}

function xmlEscape(s: string): string {
  return s.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll('"', "&quot;");
}

// number formats Excel knows by id; any other gets an id from 164
const BUILTIN_FORMATS: Map<string, int> = new Map<string, int>([
  ["General", 0], ["0", 1], ["0.00", 2], ["#,##0", 3], ["#,##0.00", 4], ["0%", 9], ["0.00%", 10], ["0.00E+00", 11],
  ["# ?/?", 12], ["mm-dd-yy", 14], ["d-mmm-yy", 15], ["d-mmm", 16], ["mmm-yy", 17], ["h:mm AM/PM", 18],
  ["h:mm:ss AM/PM", 19], ["h:mm", 20], ["h:mm:ss", 21], ["m/d/yy h:mm", 22], ["#,##0 ;(#,##0)", 37],
  ["#,##0 ;[Red](#,##0)", 38], ["#,##0.00 ;(#,##0.00)", 39], ["#,##0.00 ;[Red](#,##0.00)", 40], ["mm:ss", 45],
  ["[h]:mm:ss", 46], ["mmss.0", 47], ["##0.0E+0", 48], ["@", 49],
]);

/** stream.xlsx.WorkbookWriter: a workbook written as it is built, in constant memory. */
export class WorkbookWriter {
  creator: string = "";
  lastModifiedBy: string = "";
  created: Date = new Date();
  modified: Date = new Date();
  readonly zip: NativeZip;
  readonly styled: boolean;
  dateStyle: int = 0;
  private readonly sink: WritableSink | null;
  private readonly sheets: Worksheet[] = [];
  private current: Worksheet | null = null;
  private readonly waiting: Worksheet[] = [];
  private readonly numFmts: Map<string, int> = new Map<string, int>();
  private nextFmt: int = 164;
  private readonly fonts: string[] = ['<font><sz val="11"/><name val="Calibri"/><family val="2"/></font>'];
  private readonly fontIds: Map<string, int> = new Map<string, int>();
  private readonly xfs: string[] = ['<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>'];
  private readonly xfIds: Map<string, int> = new Map<string, int>();
  private committed: boolean = false;

  constructor(options: WorkbookWriterOptions) {
    const file: string = options.filename ?? "";
    const st = options.stream ?? null;
    this.sink = st instanceof Writable ? new NodeStreamSink(st) : st;
    if (file === "" && this.sink === null) throw new Error("exceljs: give a filename or a stream");
    this.styled = options.useStyles ?? false;
    this.zip = new NativeZip(file);
    if (this.styled) this.dateStyle = this.styleId("mm-dd-yy", null, null);
  }

  /** A new worksheet, written after the ones before it. */
  addWorksheet(name: string, options?: AddWorksheetOptions): Worksheet {
    if (this.committed) throw new Error("exceljs: the workbook is already committed");
    const views: WorksheetView[] = options?.views ?? [];
    const ws = new Worksheet(this, this.sheets.length + 1, name, views);
    this.sheets.push(ws);
    return ws;
  }

  /** (a worksheet asks to write: true when it is the one writing now, or no other is) */
  claim(ws: Worksheet): boolean {
    if (this.current === null) {
      this.current = ws;
      return true;
    }
    if (this.current === ws) return true;
    if (this.waiting.findIndex((w) => w === ws) < 0) this.waiting.push(ws);
    return false;
  }

  /** (a worksheet is committed: finish it now if it is its turn, else when the one before it is done) */
  sheetDone(ws: Worksheet): void {
    if (this.current === null || this.current === ws) {
      this.current = ws;
      ws.finish();
      this.current = null;
      this.next();
      return;
    }
    if (this.waiting.findIndex((w) => w === ws) < 0) this.waiting.push(ws);
  }

  private next(): void {
    while (this.current === null && this.waiting.length > 0) {
      const ws = this.waiting.shift()!;
      this.current = ws;
      ws.resume();
      if (ws.done) this.current = null;
      else return;
    }
  }

  /** (hand the bytes written so far to the sink) */
  drain(): void {
    const s = this.sink;
    if (s === null || this.zip.pending() === 0) return;
    s.write(this.zip.take());
  }

  /** (the style index of a number format, a font and an alignment, made once) */
  styleId(numFmt: string, font: Font | null, al: Alignment | null): int {
    let fmtId: int = 0;
    if (numFmt !== "") {
      if (BUILTIN_FORMATS.has(numFmt)) fmtId = BUILTIN_FORMATS.get(numFmt)!;
      else {
        if (this.numFmts.has(numFmt)) fmtId = this.numFmts.get(numFmt)!;
        else {
          fmtId = this.nextFmt;
          this.nextFmt++;
          this.numFmts.set(numFmt, fmtId);
        }
      }
    }
    let fontId: int = 0;
    if (font !== null) {
      let f = "<font>";
      if (font.bold ?? false) f += "<b/>";
      if (font.italic ?? false) f += "<i/>";
      f += `<sz val="${font.size ?? 11}"/><name val="${xmlEscape(font.name ?? "Calibri")}"/><family val="2"/></font>`;
      if (this.fontIds.has(f)) fontId = this.fontIds.get(f)!;
      else {
        fontId = this.fonts.length;
        this.fonts.push(f);
        this.fontIds.set(f, fontId);
      }
    }
    let align = "";
    if (al !== null) {
      const h: string = al.horizontal ?? "";
      const v: string = al.vertical ?? "";
      const wrap: boolean = al.wrapText ?? false;
      align = `<alignment${h !== "" ? ` horizontal="${xmlEscape(h)}"` : ""}${v !== "" ? ` vertical="${xmlEscape(v)}"` : ""}${wrap ? ' wrapText="1"' : ""}/>`;
    }
    let xf = `<xf numFmtId="${fmtId}" fontId="${fontId}" fillId="0" borderId="0" xfId="0"`;
    if (fmtId !== 0) xf += ' applyNumberFormat="1"';
    if (fontId !== 0) xf += ' applyFont="1"';
    xf += align !== "" ? ` applyAlignment="1">${align}</xf>` : "/>";
    if (this.xfIds.has(xf)) return this.xfIds.get(xf)!;
    const id: int = this.xfs.length;
    this.xfs.push(xf);
    this.xfIds.set(xf, id);
    return id;
  }

  /** Finish every worksheet and the workbook, and end the stream (or close the file). */
  async commit(): Promise<void> {
    if (this.committed) return;
    for (const ws of this.sheets) if (!ws.done) ws.commit();
    this.next();
    this.committed = true;
    const z = this.zip;
    z.open("[Content_Types].xml");
    let ct = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
      + '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
      + '<Default Extension="xml" ContentType="application/xml"/>'
      + '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>';
    for (const ws of this.sheets) {
      ct += `<Override PartName="/xl/worksheets/sheet${ws.id}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>`;
    }
    ct += '<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>'
      + '<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>'
      + '<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>'
      + "</Types>";
    z.text(ct);
    z.close();
    z.open("_rels/.rels");
    z.text('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
      + '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>'
      + '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>'
      + '<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>'
      + "</Relationships>");
    z.close();
    z.open("docProps/core.xml");
    const iso = (d: Date): string => d.toISOString().slice(0, 19) + "Z";
    z.text('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" '
      + 'xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" '
      + 'xmlns:dcmitype="http://purl.org/dc/dcmitype/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">'
      + `<dc:creator>${xmlEscape(this.creator)}</dc:creator>`
      + `<cp:lastModifiedBy>${xmlEscape(this.lastModifiedBy)}</cp:lastModifiedBy>`
      + `<dcterms:created xsi:type="dcterms:W3CDTF">${iso(this.created)}</dcterms:created>`
      + `<dcterms:modified xsi:type="dcterms:W3CDTF">${iso(this.modified)}</dcterms:modified>`
      + "</cp:coreProperties>");
    z.close();
    z.open("docProps/app.xml");
    z.text('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"><Application>Microsoft Excel</Application></Properties>');
    z.close();
    z.open("xl/workbook.xml");
    let wb = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
      + 'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
      + '<bookViews><workbookView/></bookViews><sheets>';
    for (const ws of this.sheets) wb += `<sheet name="${xmlEscape(ws.name)}" sheetId="${ws.id}" r:id="rId${ws.id}"/>`;
    wb += "</sheets>";
    let names = "";
    for (const ws of this.sheets) {
      if (ws.autoFilter !== null) {
        const ref = ws.absoluteFilterRef();
        names += `<definedName name="_xlnm._FilterDatabase" localSheetId="${ws.id - 1}" hidden="1">'${ws.name.replaceAll("'", "''")}'!${ref}</definedName>`;
      }
    }
    if (names !== "") wb += `<definedNames>${names}</definedNames>`;
    z.text(wb + "</workbook>");
    z.close();
    z.open("xl/_rels/workbook.xml.rels");
    let rels = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">';
    for (const ws of this.sheets) {
      rels += `<Relationship Id="rId${ws.id}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet${ws.id}.xml"/>`;
    }
    rels += `<Relationship Id="rId${this.sheets.length + 1}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>`;
    z.text(rels + "</Relationships>");
    z.close();
    z.open("xl/styles.xml");
    let st = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
      + '<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">';
    if (this.numFmts.size > 0) {
      st += `<numFmts count="${this.numFmts.size}">`;
      for (const [code, id] of this.numFmts) st += `<numFmt numFmtId="${id}" formatCode="${xmlEscape(code)}"/>`;
      st += "</numFmts>";
    }
    st += `<fonts count="${this.fonts.length}">${this.fonts.join("")}</fonts>`
      + '<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>'
      + '<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>'
      + '<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>'
      + `<cellXfs count="${this.xfs.length}">${this.xfs.join("")}</cellXfs>`
      + '<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>'
      + "</styleSheet>";
    z.text(st);
    z.close();
    z.finish();
    this.drain();
    const s = this.sink;
    if (s !== null) s.end();
  }
}

class StreamXlsx {}

/** exceljs's `stream` namespace: stream.xlsx.WorkbookWriter, in a type or after new, is the WorkbookWriter above (the
 *  compiler reads the path to the class the module exports). */
export class stream {
  static readonly xlsx: StreamXlsx = new StreamXlsx();
}
