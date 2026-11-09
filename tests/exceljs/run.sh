#!/bin/sh
# packages/exceljs: a report written as a stream (two sheets, styles, frozen pane, autoFilter) to a file and to a
# memory sink, checked as a ZIP and as XML (check.py) and, when LibreOffice is installed, opened by it; then the
# peak memory of 100 000 and 1 000 000 rows, which must be the same (constant memory).
cd "$(dirname "$0")" || exit 1
X=$(cd ../.. && pwd)/build/xisto
OUT=../../build/tests/exceljs
mkdir -p "$OUT/.xisto/pkgs"
ln -sfn "$(cd ../../packages/exceljs && pwd)" "$OUT/.xisto/pkgs/exceljs"
cp report.ts memory.ts "$OUT/"
XISTO_NODE_COMPAT=1 $X build --no-warnings -o "$OUT/report" "$OUT/report.ts" || exit 1
XISTO_NODE_COMPAT=1 $X build --no-warnings -o "$OUT/memory" "$OUT/memory.ts" || exit 1
XISTO_RC_STATS=1 "$OUT/report" "$OUT/report.xlsx" 1000 > "$OUT/report.out" 2> "$OUT/report.stats" || { echo "FAIL: report exited $?"; cat "$OUT/report.stats"; exit 1; }
if ! grep -q "ended true" "$OUT/report.out"; then echo "FAIL: the sink was not ended"; cat "$OUT/report.out"; exit 1; fi
if ! grep -q ' 0 still alive' "$OUT/report.stats"; then echo "FAIL: leak: $(cat "$OUT/report.stats")"; exit 1; fi
python3 -I check.py "$OUT/report.xlsx" 1000 > "$OUT/check.txt" 2>&1 || { echo "FAIL: check.py"; cat "$OUT/check.txt"; exit 1; }
if ! cmp -s expected.txt "$OUT/check.txt"; then echo "FAIL: workbook content"; diff expected.txt "$OUT/check.txt"; exit 1; fi
lo=skipped
if command -v soffice > /dev/null 2>&1; then
    (cd "$OUT" && rm -f report-*.csv && soffice --headless --convert-to 'csv:Text - txt - csv (StarCalc):44,34,76,1,,0,false,true,true,false,false,-1' report.xlsx > lo.log 2>&1)
    if [ "$(sed -n 4p "$OUT/report-Summary.csv")" != "First run,31/01/2026 23:59:58" ] || [ "$(wc -l < "$OUT/report-Orders.csv" | tr -d ' ')" != 1001 ]; then
        echo "FAIL: LibreOffice reading"; head -5 "$OUT/report-Summary.csv" "$OUT/report-Orders.csv"; exit 1
    fi
    lo=ok
fi
peak() { /usr/bin/time -l "$OUT/memory" "$OUT/m$1.xlsx" "$1" 2>&1 >/dev/null | awk '/maximum resident/ {print $1}'; }
[ "$(uname)" = Darwin ] || peak() { /usr/bin/time -f '%M' "$OUT/memory" "$OUT/m$1.xlsx" "$1" 2>&1 >/dev/null | tail -1 | awk '{print $1 * 1024}'; }
a=$(peak 100000); b=$(peak 1000000)
if [ -z "$a" ] || [ -z "$b" ] || [ $((b - a)) -gt 4194304 ]; then echo "FAIL: memory grows with the rows: $a -> $b bytes"; exit 1; fi
unzip -tq "$OUT/m1000000.xlsx" > /dev/null || { echo "FAIL: 1 000 000-row ZIP"; exit 1; }
echo "ok: exceljs (stream writer, styles, LibreOffice $lo, peak $((a / 1048576)) MB at 100k rows, $((b / 1048576)) MB at 1M)"
