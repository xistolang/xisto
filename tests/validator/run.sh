#!/bin/sh
# class-validator: rules checked the way class-validator 0.14 checks them (isIn compares by value and type, as
# possibleValues.some(v => v === value) does). Run from the repository root.
cd "$(dirname "$0")" || exit 1
X=../../build/xisto
mkdir -p .xisto/pkgs
for p in class-validator class-transformer; do ln -sfn "../../../../packages/$p" ".xisto/pkgs/$p"; done
fail=0
if $X run --no-warnings src/isin.ts > /tmp/xisto-validator-isin.out 2>&1 && cmp -s /tmp/xisto-validator-isin.out isin.expected; then echo "ok validator isIn"; else echo "FAIL validator isIn"; diff isin.expected /tmp/xisto-validator-isin.out | head; fail=1; fi
exit $fail
