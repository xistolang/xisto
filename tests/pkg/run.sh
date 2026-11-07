#!/bin/sh
# xisto add / install: a package from a local git repository, imported by name, locked by commit.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
set -e
X=$(cd "$(dirname "$0")/../.." && pwd)/build/xisto
W=$(mktemp -d)
cd "$W"
git init -q mathx && cd mathx
cat > index.ts <<'TS'
export function double(x: int): int {
  return x * 2;
}
export function unusedHeavy(x: int): int {
  let s: int = 0;
  for (let i: int = 0; i < x; i++) {
    s += i * i;
  }
  return s;
}
TS
printf '{\n  "name": "mathx",\n  "main": "index.ts"\n}\n' > xisto.json
git add . && git -c user.email=t@t -c user.name=t commit -q -m v1 && git tag v1
v1=$(git rev-parse HEAD)
sed -i.bak 's/return x \* 2;/return x * 3;/' index.ts && rm index.ts.bak
git -c user.email=t@t -c user.name=t commit -qam v2
cd "$W" && mkdir app && cd app
cat > main.ts <<'TS'
import { double } from "mathx";
console.log(double(21));
TS
$X build -o app main.ts 2>&1 | grep -q "is not installed\|cannot resolve" && echo "ok: missing package reported"
$X add "$W/mathx@v1" > add.txt && cat add.txt
grep -q "\"mathx\": \"$W/mathx@v1\"" xisto.json && echo "ok: xisto.json records the ref"
grep -q "$v1" xisto.lock && echo "ok: xisto.lock records the commit"
$X build -o app main.ts && [ "$(./app)" = "42" ] && echo "ok: v1 builds and prints 42"
nm app | grep -q unusedHeavy && echo "FAIL: unused package function linked" || echo "ok: unused package function not in the binary"
rm -rf .xisto && $X install > /dev/null && $X build -o app main.ts && [ "$(./app)" = "42" ] && echo "ok: install restores the locked commit (upstream has v2)"
$X add "$W/mathx" --name mathx > /dev/null && $X build -o app main.ts && [ "$(./app)" = "63" ] && echo "ok: re-adding without a ref moves to v2"
rm -rf "$W"
