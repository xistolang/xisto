#!/bin/sh
# xisto init / add NAME / install against a local git "registry" with the monorepo layout of
# github.com/xistolang/packages (<root>/@scope/name, <root>/name): subfolders, transitive dependencies,
# refs, the shared cache, errors. No network: XISTO_REGISTRY is a local repository.
X=$(cd "$(dirname "$0")/../.." && pwd)/build/xisto
W=$(mktemp -d)
export XDG_CACHE_HOME="$W/cache" # the clones of repositories: never the user's own cache
unset XISTO_PKGS
n=0
check() { # check DESCRIPTION COMMAND...: "ok: ..." or "FAIL: ..."
    d=$1; shift
    if "$@"; then echo "ok: $d"; else echo "FAIL: $d"; fi
}
commit() { git -c user.email=t@t -c user.name=t commit -q "$@"; }

# the registry: v1 = @acme/core -> strutil; v2 (main) = @acme/core -> strutil, extra; extra -> @acme/core (a cycle)
cd "$W" && git init -q -b main reg && cd reg
mkdir -p @acme/core strutil
cat > @acme/core/index.ts <<'TS'
import { shout } from "strutil";
export function greet(name: string): string {
  return shout("hello " + name);
}
TS
printf '{\n  "name": "@acme/core",\n  "version": "0.1.0",\n  "main": "index.ts",\n  "dependencies": { "strutil": "0.1.0" }\n}\n' > @acme/core/xisto.json
cat > strutil/index.ts <<'TS'
export function shout(s: string): string {
  return s.toUpperCase() + "!";
}
TS
printf '{ "name": "strutil", "version": "0.1.0", "main": "index.ts" }\n' > strutil/xisto.json
git add . && commit -m v1 && git tag v1
mkdir extra
printf 'export function tag(s: string): string {\n  return "[" + s + "]";\n}\n' > extra/index.ts
printf '{ "name": "extra", "main": "index.ts", "dependencies": { "@acme/core": "0.1.0" } }\n' > extra/xisto.json
cat > @acme/core/index.ts <<'TS'
import { shout } from "strutil";
import { tag } from "extra";
export function greet(name: string): string {
  return tag(shout("hi " + name));
}
TS
printf '{\n  "name": "@acme/core",\n  "version": "0.2.0",\n  "main": "index.ts",\n  "dependencies": { "strutil": "0.1.0", "extra": "0.1.0" }\n}\n' > @acme/core/xisto.json
git add . && commit -m v2
v1=$(git rev-parse v1)
v2=$(git rev-parse HEAD)
export XISTO_REGISTRY="$W/reg"

# xisto init
cd "$W" && "$X" init app > init.txt
check "init creates xisto.json, src/main.ts and .gitignore" test -f app/xisto.json -a -f app/src/main.ts -a -f app/.gitignore
check "init's xisto.json names the project and its main" grep -q '"main": "src/main.ts"' app/xisto.json
check "init's .gitignore ignores .xisto/ and the executable" sh -c "grep -qx '.xisto/' app/.gitignore && grep -qx '/app' app/.gitignore"
check "init prints the next steps" grep -q "xisto add <package>" init.txt
"$X" init app > init2.txt
check "init again overwrites nothing (one message per file)" test "$(grep -c 'already exists' init2.txt)" = 3
cd app
check "the hello world runs (xisto run without a file: xisto.json main)" test "$("$X" run)" = "Hello, world!"

# xisto add NAME: the package's folder of the registry, its dependencies from the same commit
cat > src/main.ts <<'TS'
import { greet } from "@acme/core";
console.log(greet("xisto"));
TS
"$X" add @acme/core > add.txt 2>&1
cat add.txt
check "add installs the scoped package and its dependencies" test -f .xisto/pkgs/@acme/core/index.ts -a -f .xisto/pkgs/strutil/index.ts -a -f .xisto/pkgs/extra/index.ts
check "a package is a copy, not a git clone" test ! -e .xisto/pkgs/@acme/core/.git -a ! -L .xisto/pkgs/@acme/core
check "the dependency cycle extra -> @acme/core is noted, not followed" grep -q "dependency cycle @acme/core -> extra -> @acme/core" add.txt
check "xisto.json lists only what was asked for" sh -c "grep -q '\"@acme/core\": \"\\*\"' xisto.json && ! grep -q strutil xisto.json && grep -q '\"main\": \"src/main.ts\"' xisto.json"
check "xisto.lock locks the package with its folder and commit" grep -q "\"@acme/core\": {\"source\": \"$W/reg\", \"subdir\": \"@acme/core\", \"commit\": \"$v2\"}" xisto.lock
check "xisto.lock locks the dependencies as indirect" grep -q "\"strutil\": {\"source\": \"$W/reg\", \"subdir\": \"strutil\", \"commit\": \"$v2\", \"indirect\": true}" xisto.lock
"$X" build > /dev/null 2>&1
check "xisto build without a file makes ./app from xisto.json main" test "$(./app)" = "[HI XISTO!]"
check "xisto build -o OUT without a file" sh -c "'$X' build -o out > /dev/null 2>&1 && test \"\$(./out)\" = '[HI XISTO!]'"
check "xisto run from a subfolder of the project" sh -c "cd src && test \"\$('$X' run)\" = '[HI XISTO!]'"

# xisto install: the locked commits again, from the cache (the registry gone)
mv "$W/reg" "$W/reg.away"
rm -rf .xisto/pkgs app
"$X" install > install.txt 2>&1
check "install restores every locked package from the cache" sh -c "test -f .xisto/pkgs/@acme/core/index.ts -a -f .xisto/pkgs/strutil/index.ts -a -f .xisto/pkgs/extra/index.ts && '$X' build > /dev/null 2>&1 && test \"\$(./app)\" = '[HI XISTO!]'"
mv "$W/reg.away" "$W/reg"

# @ref: pinned to v1 (no extra there: removed), locked with the ref
"$X" add @acme/core@v1 > pin.txt 2>&1
cat pin.txt
check "@scope/name@ref installs that revision" sh -c "'$X' build > /dev/null 2>&1 && test \"\$(./app)\" = 'HELLO XISTO!'"
check "xisto.json and xisto.lock record the ref" sh -c "grep -q '\"@acme/core\": \"v1\"' xisto.json && grep -q '\"ref\": \"v1\", \"commit\": \"$v1\"' xisto.lock"
check "an indirect dependency moves with its re-added dependent (no warning)" sh -c "! grep -q warning pin.txt && grep -q '\"strutil\": {\"source\": \"$W/reg\", \"subdir\": \"strutil\", \"commit\": \"$v1\", \"indirect\": true}' xisto.lock"
check "a dependency no package needs any more is removed" sh -c "grep -q 'removed extra' pin.txt && test ! -e .xisto/pkgs/extra && ! grep -q '\"extra\"' xisto.lock"
rm -rf .xisto
"$X" install > /dev/null 2>&1
check "install restores the pinned revision" sh -c "'$X' build > /dev/null 2>&1 && test \"\$(./app)\" = 'HELLO XISTO!'"

# a dependency already installed from another commit: kept, with a warning
"$X" add strutil@v1 > /dev/null 2>&1
"$X" add @acme/core > keep.txt 2>&1
check "a dependency locked at another commit is kept, with a warning" sh -c "grep -q 'keeping strutil at ${v1%"${v1#????????????}"}' keep.txt && grep -q '\"strutil\": {\"source\": \"$W/reg\", \"subdir\": \"strutil\", \"ref\": \"v1\", \"commit\": \"$v1\"}' xisto.lock"

# a project without a lock: xisto install resolves xisto.json
rm -rf .xisto xisto.lock
"$X" install > nolock.txt 2>&1
check "install without xisto.lock resolves xisto.json and writes the lock" sh -c "test -f xisto.lock && '$X' build > /dev/null 2>&1 && test \"\$(./app)\" = '[HI XISTO!]'"

# SOURCE#FOLDER: a monorepo given by path, the folder named in xisto.json
cd "$W" && "$X" init app2 > /dev/null && cd app2
"$X" add "$W/reg#strutil@v1" > /dev/null 2>&1
check "add SOURCE#FOLDER@REF installs that folder under its xisto.json name" sh -c "test -f .xisto/pkgs/strutil/index.ts && grep -q '\"strutil\": \"$W/reg#strutil@v1\"' xisto.json"
check "one clone of a repository in the cache, shared by projects" test "$(ls "$W/cache/xisto/git" | wc -l | tr -d ' ')" = 1

# errors: nothing written
cp xisto.json before.json
"$X" add nope > err1.txt 2>&1; rc1=$?
check "a name missing from the registry is a clear error" sh -c "test $rc1 != 0 && grep -q \"package 'nope' not found in $W/reg at ${v2%"${v2#????????????}"}\" err1.txt"
"$X" add "$W/reg#../etc" > err2.txt 2>&1; rc2=$?
check "a folder with '..' is refused" sh -c "test $rc2 = 2 && grep -q \"invalid folder '../etc'\" err2.txt"
"$X" add "Bad Name" > err3.txt 2>&1; rc3=$?
check "an invalid package name is refused" sh -c "test $rc3 = 2 && grep -q 'invalid package name' err3.txt"
"$X" add "$W/reg#/etc" > err4.txt 2>&1; rc4=$?
check "an absolute folder is refused" test $rc4 = 2
"$X" add "@acme/core@nope" > err5.txt 2>&1; rc5=$?
check "a missing revision is a clear error" sh -c "test $rc5 != 0 && grep -q \"has no revision 'nope'\" err5.txt"
check "a failed add leaves xisto.json as it was" cmp -s xisto.json before.json
"$X" add "https://user:secret@127.0.0.1:9/none.git" > err6.txt 2>&1
check "a URL's password is never printed" sh -c "! grep -q secret err6.txt"
rm -rf "$W"
