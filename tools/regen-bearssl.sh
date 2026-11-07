#!/bin/sh
# Regenerates the generated parts of the patched BearSSL (see third_party/bearssl/XISTO_PATCHES.md):
#   1. ssl_hs_client.c / ssl_hs_server.c from the .t0 sources, with BearSSL's own T0 compiler
#      (C#, built here with the .NET SDK: brew install dotnet);
#   2. ec_p256_m64_comb.h, the fixed-base table for P-256, from the original window code.
# Run it after editing a .t0 file. Both outputs are checked in, so a normal build never needs .NET.
set -e
cd "$(dirname "$0")/.."
B=third_party/bearssl
OUT=build/t0comp
dotnet build tools/t0comp/T0Comp.csproj -c Release -o $OUT > /dev/null
for side in client server; do
    dotnet $OUT/T0Comp.dll -o $B/src/ssl/ssl_hs_$side -r br_ssl_hs_$side $B/src/ssl/ssl_hs_common.t0 $B/src/ssl/ssl_hs_$side.t0 > /dev/null
done
cc -O2 -I$B/inc -I$B/src -o build/gen_p256_comb tools/gen_p256_comb.c build/libbearssl.a
build/gen_p256_comb > $B/src/ec/ec_p256_m64_comb.h
echo "regenerated: ssl_hs_client.c ssl_hs_server.c ec_p256_m64_comb.h"
