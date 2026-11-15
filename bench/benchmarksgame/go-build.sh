#!/usr/bin/env bash
# Builds every official Go program of the Benchmarks Game (the BSD-3 source package, not only the fastest)
# for the ten problems that bench/benchmarksgame/langs.py runs, so the runner can measure them:
#
#   bench/benchmarksgame/go-build.sh [DIR]          # default DIR: ~/gobg
#   python3 bench/benchmarksgame/langs.py 3 go=DIR/bin
#
# Executables are named as langs.py globs them (DIR/bin/{problem} and DIR/bin/{problem}_*): program #1
# (binarytrees.go) becomes bin/binarytrees, program #N (binarytrees.go-N.go) becomes bin/binarytrees_N.
#
# Each program is built as the site builds it: plain `go build` in one module that holds the third-party
# packages (github.com/ncw/gmp, two PCRE bindings). On x86-64, GOAMD64 plays the role of the site's ivybridge
# target: v3 when the CPU has AVX2 (like Rust's target-cpu=native), v2 otherwise. Set GOAMD64 to override.
#
# Needs: go (PATH or /usr/local/go/bin), curl, unzip, a C compiler (cgo), GMP and PCRE (PCRE1, not PCRE2):
#   macOS:          brew install go gmp pcre
#   Ubuntu:         sudo apt-get install -y build-essential unzip libgmp-dev libpcre3-dev
#   Amazon Linux:   sudo dnf install -y gcc unzip gmp-devel pcre-devel
# A program whose library is missing fails alone and is listed at the end; the others are still built.
# No program uses x86-specific code, so all 48 build on arm64 too. Known output notes (langs.md):
#   mandelbrot, mandelbrot_3, mandelbrot_4 take c as (2/N)*x instead of 2*x/N: different doubles, one boundary
#   pixel differs from Node/xisto (a different computation, excluded like Rust's nbody_7/9).
#   regexredux_5 needs PCRE JIT; Homebrew's arm64 PCRE1 has no JIT, so on macOS it panics with "study error".
set -u

DIR=${1:-$HOME/gobg}
ZIP_URL=https://salsa.debian.org/benchmarksgame-team/benchmarksgame/-/raw/master/public/download/benchmarksgame-sourcecode.zip
PROBLEMS="spectralnorm nbody fannkuchredux binarytrees mandelbrot fasta knucleotide revcomp regexredux pidigits"

export PATH="$PATH:/usr/local/go/bin"
command -v go >/dev/null || { echo "go not found (PATH or /usr/local/go/bin)" >&2; exit 1; }
mkdir -p "$DIR/bin" "$DIR/mod"
DIR=$(cd "$DIR" && pwd)

# Homebrew's libraries on macOS (cgo does not look in /opt/homebrew by itself).
if [ "$(uname -s)" = Darwin ]; then
  BREW=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
  export CGO_CFLAGS="${CGO_CFLAGS:-} -I$BREW/include"
  export CGO_LDFLAGS="${CGO_LDFLAGS:-} -L$BREW/lib"
fi
export CGO_ENABLED=1
if [ "$(uname -m)" = x86_64 ] && [ -z "${GOAMD64:-}" ]; then
  if grep -qw avx2 /proc/cpuinfo 2>/dev/null || sysctl -n machdep.cpu.leaf7_features 2>/dev/null | grep -qw AVX2; then
    export GOAMD64=v3
  else
    export GOAMD64=v2
  fi
fi
echo "# $(go version)${GOAMD64:+, GOAMD64=$GOAMD64}"

# 1. The official source package.
if [ ! -d "$DIR/unz" ]; then
  curl -sSLf -o "$DIR/src.zip" "$ZIP_URL" || { echo "download failed" >&2; exit 1; }
  unzip -q -o "$DIR/src.zip" -d "$DIR/unz" || exit 1
fi

# 2. One module with every program in its own package directory, so the third-party packages are fetched once.
cd "$DIR/mod"
[ -f go.mod ] || go mod init gobg >/dev/null 2>&1
for p in $PROBLEMS; do
  for src in "$DIR/unz/$p/$p".go "$DIR/unz/$p/$p".go-*.go; do
    [ -f "$src" ] || continue
    b=$(basename "$src")
    case $b in
      "$p.go") name=$p ;;
      *) n=${b#"$p.go-"}; name=${p}_${n%.go} ;;
    esac
    mkdir -p "$name"
    cp "$src" "$name/main.go"
  done
done
# Fetches whatever the programs import (github.com/ncw/gmp, github.com/GRbit/go-pcre,
# github.com/mdellandrea/golang-pkg-pcre); -e keeps going if one of them cannot be resolved.
# go-pcre is pinned at v1.0.0: regexredux #5 uses Regexp.Matcher, which v1.0.1 and later removed.
go get github.com/GRbit/go-pcre@v1.0.0 >/dev/null 2>&1
go mod tidy -e >/dev/null 2>&1

ok=() failed=()
for d in */; do
  name=${d%/}
  if out=$(go build -o "$DIR/bin/$name" "./$name" 2>&1); then
    ok+=("$name")
  else
    failed+=("$name")
    echo "-- $name failed:" >&2
    echo "$out" | head -5 >&2
  fi
done
echo "built (${#ok[@]}): ${ok[*]-}"
[ ${#failed[@]} -eq 0 ] || echo "FAILED (${#failed[@]}): ${failed[*]}"
echo "run: python3 bench/benchmarksgame/langs.py 3 go=$DIR/bin"
