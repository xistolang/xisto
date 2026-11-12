#!/bin/sh
# packages/mongoose + packages/@nestjs/mongoose (stage 1, no MongoDB driver). main.ts and feature.ts against
# expected-main.txt / expected-feature.txt: the output of the same sources on Node with mongoose 8.24.8,
# @nestjs/mongoose 11.0.4 and Nest 11 (tsc, emitDecoratorMetadata) — schemas from @Schema/@Prop, ObjectId, tokens,
# @Optional() model/connection null without the Mongo modules, forFeature models injected. offline.ts: with no
# server (a closed port) server selection fails as on Node (MongooseServerSelectionError, events, bufferCommands:
# false, Nest's retries). No leaks in any of them.
cd "$(dirname "$0")" || exit 1
X=$(cd ../.. && pwd)/build/xisto
OUT=../../build/tests/mongoose
mkdir -p "$OUT/.xisto/pkgs/@nestjs"
P=$(cd ../../packages && pwd)
for p in "$P"/*; do case $(basename "$p") in @*|xisto) ;; *) ln -sfn "$p" "$OUT/.xisto/pkgs/$(basename "$p")";; esac; done
for p in "$P"/@nestjs/*; do ln -sfn "$p" "$OUT/.xisto/pkgs/@nestjs/$(basename "$p")"; done
cp main.ts feature.ts offline.ts schemas.ts "$OUT/"
for t in main feature offline; do
  $X build -o "$OUT/$t" "$OUT/$t.ts" > "$OUT/$t.build.txt" 2>&1 || { echo "FAIL: build $t"; cat "$OUT/$t.build.txt"; exit 1; }
  XISTO_RC_STATS=1 "$OUT/$t" > "$OUT/$t.txt" 2> "$OUT/$t.stats.txt"
  if ! cmp -s "expected-$t.txt" "$OUT/$t.txt"; then echo "FAIL: $t output"; diff "expected-$t.txt" "$OUT/$t.txt"; exit 1; fi
  # (feature.ts ends with process.exit, which prints no counts)
  if [ $t != feature ] && ! grep -q ' 0 still alive' "$OUT/$t.stats.txt"; then echo "FAIL: $t leak: $(cat "$OUT/$t.stats.txt")"; exit 1; fi
done
echo "ok: mongoose (schemas, ObjectId, Nest injection as on Node; no server: selection errors as on Node)"
# against a live MongoDB (skipped without XISTO_MONGO_URL=mongodb://127.0.0.1:27017; databases xisto_live_test and
# xisto_nest_test): live.ts (the operations, what they write read back raw) and nest-live.ts (a Nest app's Mongo
# wiring) against expected-live.txt / expected-nest-live.txt, the same sources on Node with mongoose 8.24.8,
# @nestjs/mongoose 11.0.4 and Nest 11 against the same server
if [ -z "$XISTO_MONGO_URL" ]; then echo "skip: XISTO_MONGO_URL is not set (mongodb://127.0.0.1:27017)"; exit 0; fi
cp live.ts nest-live.ts live-schemas.ts "$OUT/"
for t in live nest-live; do
  $X build -o "$OUT/$t" "$OUT/$t.ts" > "$OUT/$t.build.txt" 2>&1 || { echo "FAIL: build $t"; cat "$OUT/$t.build.txt"; exit 1; }
  XISTO_RC_STATS=1 "$OUT/$t" > "$OUT/$t.txt" 2> "$OUT/$t.stats.txt"
  if ! cmp -s "expected-$t.txt" "$OUT/$t.txt"; then echo "FAIL: $t output"; diff "expected-$t.txt" "$OUT/$t.txt"; exit 1; fi
  if ! grep -q ' 0 still alive' "$OUT/$t.stats.txt"; then echo "FAIL: $t leak: $(cat "$OUT/$t.stats.txt")"; exit 1; fi
done
echo "ok: mongoose against a live MongoDB (operations and a Nest app as on Node)"
