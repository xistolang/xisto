#!/bin/sh
# reflect-metadata's global Reflect: metadata by target and property, as Node's reflect-metadata 0.2 keeps it
# (meta.expected is Node's output). Run from the repository root.
cd "$(dirname "$0")" || exit 1
X=../../build/xisto
mkdir -p .xisto/pkgs
ln -sfn "../../../../packages/reflect-metadata" ".xisto/pkgs/reflect-metadata"
T=${TMPDIR:-/tmp}/xisto-reflectmeta.$$
if $X run --no-warnings src/meta.ts > "$T" 2>&1 && cmp -s "$T" meta.expected; then
  echo "ok reflectmeta"
  rm -f "$T"
else
  echo "FAIL reflectmeta"
  diff meta.expected "$T" | head -20
  rm -f "$T"
  exit 1
fi
