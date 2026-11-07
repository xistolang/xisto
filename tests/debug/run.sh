#!/bin/sh
# The debugger (docs/debugger.md). Scripts: X.xdb (or X.NAME.xdb) drives `xisto debug --batch` on X.ts and the
# transcript must equal X[.NAME].expected; NAME starting with qbe runs it on the QBE backend. Then dap_test.py drives the agent as an IDE would.
cd "$(dirname "$0")" || exit 1
X=../../build/xisto
OUT=../../build/tests/debug
mkdir -p "$OUT"
fail=0
for s in *.xdb; do
    name=${s%.xdb}
    prog=${name%%.*}.ts
    flags=
    case $name in *.qbe*) flags=--backend=qbe ;; esac   # the same debugger over the QBE backend
    XISTO_RC_STATS=1 $X debug $flags --batch "$s" "$prog" > "$OUT/$name.out" 2>&1
    if cmp -s "$name.expected" "$OUT/$name.out"; then echo "ok: $name"; else echo "FAIL: $name"; diff "$name.expected" "$OUT/$name.out" | head -20; fail=1; fi
done
python3 dap_test.py "$OUT" || fail=1
exit $fail
