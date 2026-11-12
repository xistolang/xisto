#!/bin/sh
# Packs a built tree (make) into dist/xisto-VERSION-OS-ARCH.tar.gz: what xisto needs next to its executable to
# compile programs (the runtime itself is embedded in the executable), the packages and the licenses.
# Layout: xisto-VERSION/{build/..., packages/, LICENSE}; bin/xisto is a link to build/xisto.
# Usage: sh tools/dist.sh VERSION
set -eu
VERSION=${1:?usage: sh tools/dist.sh VERSION}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
case "$(uname -s)" in Darwin) OS=macos ;; Linux) OS=linux ;; *) echo "dist: unsupported system $(uname -s)" >&2; exit 1 ;; esac
case "$(uname -m)" in arm64|aarch64) ARCH=arm64 ;; x86_64|amd64) ARCH=x64 ;; *) echo "dist: unsupported machine $(uname -m)" >&2; exit 1 ;; esac
NAME=xisto-$VERSION
OUT=$ROOT/dist
STAGE=$OUT/$NAME
cd "$ROOT"
for f in build/xisto build/qbe build/xisto_rt.o build/xisto_rt_dbg.o build/sqlite3.o build/sqlite3.h build/libbearssl.a build/bearssl-inc; do
    [ -e "$f" ] || { echo "dist: $f is missing: run make first" >&2; exit 1; }
done
got=$(build/xisto --version)
[ "$got" = "xisto $VERSION" ] || { echo "dist: build/xisto says '$got', expected 'xisto $VERSION' (make VERSION=$VERSION)" >&2; exit 1; }

rm -rf "$STAGE" && mkdir -p "$STAGE/build" "$STAGE/bin"
cp build/xisto build/qbe build/xisto_rt.o build/xisto_rt_dbg.o build/sqlite3.o build/sqlite3.h build/libbearssl.a "$STAGE/build/"
cp -R build/bearssl-inc "$STAGE/build/"
if [ -f build/awslc/lib/libssl.a ]; then
    mkdir -p "$STAGE/build/awslc"
    cp -R build/awslc/lib build/awslc/include "$STAGE/build/awslc/"
    rm -rf "$STAGE/build/awslc/lib/cmake" "$STAGE/build/awslc/lib/pkgconfig"
fi
cp -R packages "$STAGE/packages"
find "$STAGE/packages" \( -name node_modules -o -name '*.o' -o -name .DS_Store \) -prune -exec rm -rf {} +
cp LICENSE "$STAGE/"
ln -s ../build/xisto "$STAGE/bin/xisto"

TAR=$OUT/$NAME-$OS-$ARCH.tar.gz
tar -C "$OUT" -czf "$TAR" "$NAME"
if command -v sha256sum >/dev/null; then sha256sum "$TAR" | cut -d' ' -f1 > "$TAR.sha256"; else shasum -a 256 "$TAR" | cut -d' ' -f1 > "$TAR.sha256"; fi
echo "$TAR"
echo "sha256 $(cat "$TAR.sha256")"
