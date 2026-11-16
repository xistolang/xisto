#!/bin/sh
# xisto add / install: a package from a local git repository, imported by name, locked by commit.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
set -e
X=$(cd "$(dirname "$0")/../.." && pwd)/build/xisto
W=$(mktemp -d)
export XDG_CACHE_HOME="$W/cache" # the clones of repositories: never the user's own cache
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
# a lock written before folders and refs ({"source", "commit"}): still installs
printf '{\n  "mathx": {"source": "%s", "commit": "%s"}\n}\n' "$W/mathx" "$v1" > xisto.lock
rm -rf .xisto && $X install > /dev/null && $X build -o app main.ts && [ "$(./app)" = "42" ] && echo "ok: an old xisto.lock still installs"
# import x from "pkg" of a package with no default export: the package as a namespace (XB-222), as esModuleInterop reads it
mkdir -p "$W/nsapp/.xisto/pkgs/nodef" "$W/nsapp/.xisto/pkgs/withdef" && cd "$W/nsapp"
cat > .xisto/pkgs/nodef/index.ts <<'TS'
export function double(x: int): int {
  return x * 2;
}
export const NAME = "nodef";
export class Tracer {
  startSpan(n: string): string {
    return "span:" + n;
  }
}
TS
printf 'export default function hello(): string {\n  return "hi";\n}\n' > .xisto/pkgs/withdef/index.ts
cat > main.ts <<'TS'
import nodef from "nodef";
import hello from "withdef";
const t: nodef.Tracer = new nodef.Tracer();
console.log(nodef.double(21), nodef.NAME, t.startSpan("a"), hello());
TS
$X build -o app main.ts && [ "$(./app)" = "42 nodef span:a hi" ] && echo "ok: import x from a package without a default export reads the package"
# import "pkg" (only for its effects, XB-184): the package's top level runs, once, before the importer's, also when
# another module imports it by name; a deep import ("pkg/register") too; an effect import of a package not there fails
mkdir -p "$W/fxapp/.xisto/pkgs/polyfx" && cd "$W/fxapp"
cat > .xisto/pkgs/polyfx/index.ts <<'TS'
export const installed: string[] = [];
installed.push("polyfx");
console.log("polyfx top");
TS
cat > .xisto/pkgs/polyfx/register.ts <<'TS'
import { installed } from "./index.ts";
installed.push("register");
console.log("polyfx/register top");
TS
cat > other.ts <<'TS'
import { installed } from "polyfx";
export function count(): number {
  return installed.length;
}
TS
cat > main.ts <<'TS'
import "polyfx";
import "polyfx/register";
import { count } from "./other.ts";
import "polyfx";
console.log("main", count());
TS
want=$(printf 'polyfx top\npolyfx/register top\nmain 2')
$X build -o app main.ts && [ "$(./app)" = "$want" ] && echo "ok: import \"pkg\" runs the package's top level once, in order"
printf 'import "nothere";\nconsole.log("x");\n' > missing.ts
$X build -o app missing.ts 2>&1 | grep -q "cannot resolve 'nothere'" && echo "ok: import \"pkg\" of a missing package is reported"
printf 'import "reflect-metadata";\nconsole.log("no package");\n' > refl.ts
$X build -o app refl.ts && [ "$(./app)" = "no package" ] && echo "ok: import \"reflect-metadata\" without the package still builds (as before)"
# Intl is the intl package's (xisto add intl), not built in: Intl.X is its class Intl_X, and n.toLocaleString(locale),
# d.toLocaleDateString(locale) and s.localeCompare(t, locale) go to its __intl_* functions; without it, an error
mkdir -p "$W/intlapp/.xisto/pkgs/intl" && cd "$W/intlapp"
cat > .xisto/pkgs/intl/index.ts <<'TS'
export interface Intl_NumberFormatOptions {
  style?: string;
}
export class Intl_NumberFormat {
  tag: string;
  constructor(locales: string | null = null, options: Intl_NumberFormatOptions | null = null) {
    this.tag = (locales ?? "default") + "/" + ((options ?? {}).style ?? "decimal");
  }
  format(n: number): string {
    return "nf(" + this.tag + "):" + n;
  }
}
export function __intl_number_to_locale(n: number, locales: string | null = null, options: Intl_NumberFormatOptions | null = null): string {
  return new Intl_NumberFormat(locales, options).format(n);
}
export function __intl_date_to_locale(d: Date, kind: string, locales: string | null = null, options: Intl_NumberFormatOptions | null = null): string {
  return kind + "(" + (locales ?? "") + "):" + d.getUTCFullYear();
}
export function __intl_locale_compare(a: string, b: string, locales: string | null = null, options: Intl_NumberFormatOptions | null = null): number {
  return a < b ? -2 : 2;
}
TS
cat > main.ts <<'TS'
const o: Intl.NumberFormatOptions = { style: "percent" };
console.log(new Intl.NumberFormat("pt-BR", o).format(1));
const d = new Date(Date.UTC(2026, 0, 2));
console.log((5).toLocaleString("pt-BR"), d.toLocaleDateString("pt-BR"), d.toLocaleString("en-US", {}), "a".localeCompare("b", "pt-BR"));
TS
want=$(printf 'nf(pt-BR/percent):1\nnf(pt-BR/decimal):5 date(pt-BR):2026 any(en-US):2026 -2')
$X build -o app main.ts && [ "$(./app)" = "$want" ] && echo "ok: Intl and toLocaleString / localeCompare with a locale are the intl package's"
cat > plain.ts <<'TS'
console.log((5).toLocaleString(), "a".localeCompare("b"));
TS
$X build -o app plain.ts && [ "$(./app)" = "5 -1" ] && echo "ok: toLocaleString() and localeCompare(t) without a locale stay built in"
rm -rf .xisto
$X build -o app main.ts 2>&1 | grep -q "package 'intl' is not installed" && $X build -o app main.ts 2>&1 | grep -q "xisto add intl" && echo "ok: Intl without the intl package: an error that says xisto add intl"
printf 'console.log((1234.5).toLocaleString("pt-BR"));\n' > builtin.ts
$X build -o app builtin.ts && [ "$(./app)" = "1.234,5" ] && echo "ok: n.toLocaleString(locale) without the package: the built-in pt-BR / en-US form (as before)"
cd "$W"
rm -rf "$W"
