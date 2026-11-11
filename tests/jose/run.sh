#!/bin/sh
# packages/jose (RS256). main.ts against expected.txt, the output of the same program on Node with jose 5.10.0:
# RS256 is deterministic, so the tokens signed with the fixed key (private.pem, a test key made for this) are the
# same bytes; claim checks and errors are jose's. gen.ts generates a key pair and signs with it: OpenSSL checks
# the key (primes, d, CRT values) and the signature.
cd "$(dirname "$0")" || exit 1
X=$(cd ../.. && pwd)/build/xisto
OUT=../../build/tests/jose
mkdir -p "$OUT/.xisto/pkgs"
ln -sfn "$(cd ../../packages/jose && pwd)" "$OUT/.xisto/pkgs/jose"
cp main.ts gen.ts "$OUT/"
XISTO_NODE_COMPAT=1 $X build -o "$OUT/main" "$OUT/main.ts" > "$OUT/build.txt" 2>&1 || { cat "$OUT/build.txt"; exit 1; }
XISTO_RC_STATS=1 "$OUT/main" . > "$OUT/out.txt" 2> "$OUT/stats.txt"
if ! cmp -s expected.txt "$OUT/out.txt"; then echo "FAIL: output"; diff expected.txt "$OUT/out.txt"; exit 1; fi
if ! grep -q ' 0 still alive' "$OUT/stats.txt"; then echo "FAIL: leak: $(cat "$OUT/stats.txt")"; exit 1; fi
XISTO_NODE_COMPAT=1 $X build -o "$OUT/gen" "$OUT/gen.ts" > "$OUT/build.txt" 2>&1 || { cat "$OUT/build.txt"; exit 1; }
if ! "$OUT/gen" "$OUT" | grep -q "generated in under 5 s"; then echo "FAIL: key generation"; exit 1; fi
if command -v openssl > /dev/null 2>&1; then
  if ! openssl pkey -in "$OUT/gen-private.pem" -check -noout 2>&1 | grep -q "Key is valid"; then echo "FAIL: openssl rejects the generated key"; exit 1; fi
  cut -d. -f1,2 "$OUT/gen-token.txt" | tr -d '\n' > "$OUT/gen-input.txt"
  cut -d. -f3 "$OUT/gen-token.txt" | tr '_-' '/+' | awk '{ l = length($0) % 4; if (l == 2) $0 = $0 "=="; else if (l == 3) $0 = $0 "="; print }' | openssl base64 -d -A > "$OUT/gen-sig.bin"
  if ! openssl dgst -sha256 -verify "$OUT/gen-public.pem" -signature "$OUT/gen-sig.bin" "$OUT/gen-input.txt" | grep -q "Verified OK"; then echo "FAIL: openssl does not verify the signature"; exit 1; fi
fi
echo "ok: jose RS256 (same tokens as Node's jose, claim errors, JWK, generated key checked by OpenSSL)"
