"""Structural check of the workbook tests/exceljs/report.ts writes: a valid ZIP (CRCs), well-formed XML in every
part, the parts an .xlsx needs, and the cells of both sheets (inline strings, numbers, booleans, Excel serial
dates, styles). Prints what it found; exits 1 on the first problem."""
import sys, zipfile, xml.etree.ElementTree as ET

NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
path, rows = sys.argv[1], int(sys.argv[2])
z = zipfile.ZipFile(path)
bad = z.testzip()
if bad:
    sys.exit(f"bad CRC in {bad}")
need = ["[Content_Types].xml", "_rels/.rels", "xl/workbook.xml", "xl/_rels/workbook.xml.rels", "xl/styles.xml",
        "docProps/core.xml", "docProps/app.xml", "xl/worksheets/sheet1.xml", "xl/worksheets/sheet2.xml"]
names = z.namelist()
for n in need:
    if n not in names:
        sys.exit(f"missing part {n}")
for n in names:
    ET.fromstring(z.read(n))  # well-formed

wb = ET.fromstring(z.read("xl/workbook.xml"))
sheets = [s.get("name") for s in wb.find("m:sheets", NS)]
dn = wb.find("m:definedNames", NS)
print("sheets", sheets, "filter", dn[0].text if dn is not None else None)


def cells(part):
    ws = ET.fromstring(z.read(part))
    out = []
    for r in ws.find("m:sheetData", NS):
        row = []
        for c in r:
            t, s = c.get("t"), c.get("s")
            if t == "inlineStr":
                v = "".join(x.text or "" for x in c.iter("{%s}t" % NS["m"]))
            else:
                e = c.find("m:v", NS)
                v = e.text if e is not None else None
            row.append((c.get("r"), t, s, v))
        out.append((r.get("r"), row))
    return ws, out


ws1, s1 = cells("xl/worksheets/sheet1.xml")
for r, row in s1:
    print("summary", r, [(ref, t, v) for ref, t, s, v in row])
ws2, s2 = cells("xl/worksheets/sheet2.xml")
if len(s2) != rows + 1:
    sys.exit(f"orders: {len(s2)} rows, expected {rows + 1}")
pane = ws2.find("m:sheetViews/m:sheetView/m:pane", NS)
af = ws2.find("m:autoFilter", NS)
cols = ws2.find("m:cols", NS)
print("orders rows", len(s2), "pane", pane.get("ySplit"), pane.get("state"), "autoFilter", af.get("ref"),
      "widths", [c.get("width") for c in cols])
print("orders 1", s2[0][1])
print("orders 2", s2[1][1])
print("orders last", s2[-1][1])
st = ET.fromstring(z.read("xl/styles.xml"))
print("numFmts", [(f.get("numFmtId"), f.get("formatCode")) for f in st.find("m:numFmts", NS)],
      "fonts", len(st.find("m:fonts", NS)), "xfs", len(st.find("m:cellXfs", NS)))
