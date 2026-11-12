#!/bin/sh
# Nest's Logger on the console: object messages printed as Nest 11's ConsoleLogger prints them (util.inspect), the
# context, an error's text after an object. logger.expected is Node's output (NO_COLOR=1) with the pid and the time
# masked. Run from the repository root.
cd "$(dirname "$0")" || exit 1
X=../../build/xisto
mkdir -p .xisto/pkgs/@nestjs
for p in express rxjs class-validator class-transformer; do ln -sfn "../../../../packages/$p" ".xisto/pkgs/$p"; done
ln -sfn "../../../../../packages/@nestjs/common" ".xisto/pkgs/@nestjs/common"
T=${TMPDIR:-/tmp}/xisto-nestlog.$$
if $X run --no-warnings src/logger.ts > "$T" 2>&1 && sed -E 's/^\[Nest\] [0-9]+  - [0-9/]+, [0-9:]+ [AP]M/[Nest] PID  - DATE/' "$T" | cmp -s - logger.expected; then
  echo "ok nestlog"
  rm -f "$T"
else
  echo "FAIL nestlog"
  sed -E 's/^\[Nest\] [0-9]+  - [0-9/]+, [0-9:]+ [AP]M/[Nest] PID  - DATE/' "$T" | diff logger.expected - | head -20
  rm -f "$T"
  exit 1
fi
