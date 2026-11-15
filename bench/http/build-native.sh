#!/bin/sh
# Builds the Rust (axum on tokio) and Go (net/http) servers and clients of the HTTP benchmarks into bench/out:
#   bench/out/rust-{hello,todo,deps,client}   cargo build --release (lto = "thin", codegen-units = 1), stripped
#   bench/out/go-{hello,todo,deps,client}     go build, stripped (-ldflags="-s -w")
# Run from anywhere; the section scripts call it once. A missing toolchain skips that language (exit 0): the
# section scripts then skip its targets.
# Toolchains: cargo on PATH or `rustup run stable`; go on PATH or /opt/homebrew/bin/go.
cd "$(dirname "$0")/../.." || exit 1
OUT=bench/out; mkdir -p "$OUT"
command -v cargo > /dev/null 2>&1 || { t=$(rustup which --toolchain stable cargo 2>/dev/null) && PATH=$(dirname "$t"):$PATH; }
GO=$(command -v go || echo /opt/homebrew/bin/go)
status=0
if command -v cargo > /dev/null 2>&1; then
    if cargo build --quiet --release --locked --manifest-path bench/http/rust/Cargo.toml --target-dir "$OUT/rust-target"; then
        for b in hello todo deps client; do
            cp "$OUT/rust-target/release/rust-$b" "$OUT/rust-$b" && strip "$OUT/rust-$b"
        done
    else
        echo "build-native: the Rust build failed" >&2; status=1
    fi
else
    echo "build-native: cargo not found, the rust targets are skipped" >&2
fi
if [ -x "$GO" ]; then
    for b in hello todo deps client; do
        (cd bench/http/go && "$GO" build -trimpath -ldflags="-s -w" -o "../../../$OUT/go-$b" "./cmd/$b") || { echo "build-native: the Go build of $b failed" >&2; status=1; }
    done
else
    echo "build-native: go not found, the go targets are skipped" >&2
fi
# one line for the results' headers: toolchain and crate versions
lockv() { awk -v n="$1" '$0 == "name = \"" n "\"" { getline; gsub(/version = |"/, ""); print; exit }' bench/http/rust/Cargo.lock; }
{
    command -v cargo > /dev/null 2>&1 && printf 'rustc %s (axum %s, tokio %s, serde_json %s, reqwest %s)' \
        "$(rustc --version | awk '{print $2}')" "$(lockv axum)" "$(lockv tokio)" "$(lockv serde_json)" "$(lockv reqwest)"
    [ -x "$GO" ] && printf ', go %s' "$("$GO" env GOVERSION | sed 's/^go//')"
    echo
} > "$OUT/native-versions.txt"
exit $status
