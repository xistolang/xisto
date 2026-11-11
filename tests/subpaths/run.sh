#!/bin/sh
# Deep imports of packages (a subpath is a file of the package): rxjs/operators, @nestjs/common's
# interfaces/external/cors-options.interface, and Type<T>. expected.txt is the output of main.ts on Node with
# rxjs 7, @nestjs/common 10 and reflect-metadata (tsx, experimentalDecorators).
cd "$(dirname "$0")" || exit 1
X=$(cd ../.. && pwd)/build/xisto
P=$(cd ../../packages && pwd)
OUT=../../build/tests/subpaths
rm -rf "$OUT" && mkdir -p "$OUT/.xisto/pkgs/@nestjs"
for p in "$P"/*; do n=$(basename "$p"); case $n in @*|xisto) continue;; esac; ln -s "$p" "$OUT/.xisto/pkgs/$n"; done
for p in "$P"/@nestjs/*; do ln -s "$p" "$OUT/.xisto/pkgs/@nestjs/$(basename "$p")"; done
cp main.ts "$OUT/main.ts"
XISTO_NODE_COMPAT=1 $X build -o "$OUT/main" "$OUT/main.ts" || exit 1
XISTO_RC_STATS=1 "$OUT/main" > "$OUT/out.txt" 2> "$OUT/stats.txt"
if ! cmp -s expected.txt "$OUT/out.txt"; then echo "FAIL: output"; diff expected.txt "$OUT/out.txt"; exit 1; fi
if ! grep -q ' 0 still alive' "$OUT/stats.txt"; then echo "FAIL: leak: $(cat "$OUT/stats.txt")"; exit 1; fi
# a subpath the package does not have: the error names the import and the file looked for
printf 'import { x } from "rxjs/nothing";\nconsole.log(x);\n' > "$OUT/bad.ts"
if $X build -o "$OUT/bad" "$OUT/bad.ts" > "$OUT/bad.txt" 2>&1 || ! grep -q "cannot resolve 'rxjs/nothing'" "$OUT/bad.txt" || ! grep -q "has no file 'nothing'" "$OUT/bad.txt"; then
  echo "FAIL: missing subpath"; cat "$OUT/bad.txt"; exit 1
fi
echo "ok: package subpaths (rxjs/operators, @nestjs/common deep import, Type<T>, missing subpath error)"
