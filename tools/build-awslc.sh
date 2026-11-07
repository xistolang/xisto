#!/bin/sh
# Builds AWS-LC (the default TLS backend) into build/awslc/{include,lib}.
# The source is a pinned release, downloaded once into third_party/aws-lc/ and checked by SHA-256.
# Only programs that can use https link it (see `xisto build --help`); BearSSL stays as `make TLS=bearssl`.
set -eu
VERSION=1.73.0
SHA256=e33ae89e7d09d7b23a900f68b62088d8813c260ac564b016e543ee3540ebcce3
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DL="$ROOT/third_party/aws-lc"
TAR="$DL/aws-lc-$VERSION.tar.gz"
SRC="$DL/aws-lc-$VERSION"
OUT="$ROOT/build/awslc"
BLD="$ROOT/build/awslc-build"

command -v cmake >/dev/null || { echo "AWS-LC needs cmake (brew install cmake / apt install cmake), or build with: make TLS=bearssl" >&2; exit 1; }

mkdir -p "$DL"
if [ ! -f "$TAR" ]; then
    echo "downloading AWS-LC $VERSION (145 MB, once)"
    URL="https://github.com/aws/aws-lc/archive/refs/tags/v$VERSION.tar.gz"
    if command -v curl >/dev/null; then curl -sSLf -o "$TAR.part" "$URL"; else wget -q -O "$TAR.part" "$URL"; fi
    mv "$TAR.part" "$TAR"
fi
if command -v sha256sum >/dev/null; then got=$(sha256sum "$TAR" | cut -d' ' -f1); else got=$(shasum -a 256 "$TAR" | cut -d' ' -f1); fi
[ "$got" = "$SHA256" ] || { echo "AWS-LC tarball checksum mismatch: $got (expected $SHA256); delete $TAR and retry" >&2; exit 1; }
[ -d "$SRC" ] || tar xzf "$TAR" -C "$DL"

GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN=Ninja
# DISABLE_GO/PERL: use the pre-generated assembly shipped in the release; no tools, no tests
cmake -S "$SRC" -B "$BLD" -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OUT" \
      -DBUILD_TESTING=OFF -DBUILD_TOOL=OFF -DDISABLE_GO=ON -DDISABLE_PERL=ON -DCMAKE_INSTALL_LIBDIR=lib \
      -Wno-dev > "$ROOT/build/awslc-cmake.log" 2>&1 || { cat "$ROOT/build/awslc-cmake.log" >&2; exit 1; }
cmake --build "$BLD" --target install -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" > "$ROOT/build/awslc-build.log" 2>&1 \
      || { tail -40 "$ROOT/build/awslc-build.log" >&2; exit 1; }
rm -rf "$BLD"
echo "AWS-LC $VERSION -> build/awslc"
