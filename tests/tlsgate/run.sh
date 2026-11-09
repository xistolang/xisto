#!/bin/sh
# TLS is linked only when a program can use https (an https:// URL, a fetch URL known only at run time,
# listen with cert/key). --tls=off drops it and turns the static https cases into compile errors.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.."
X=build/xisto; D=tests/tlsgate; O=${TMPDIR:-/tmp}/xisto-tlsgate.$$; mkdir -p $O
fail=0
check() { # file flags expected-tls-line
    got=$($X build --time $2 -o $O/out $D/$1.ts 2>&1 | grep -E "^xisto: tls|error(\[XS[0-9]+\])?:" | head -1 | sed -E 's/^.*error(\[XS[0-9]+\])?: /error: /')  # (the code, error[XS7059], is not part of the check)
    case "$got" in "$3"*) echo "ok   $1 ${2:-default}: $got";; *) echo "FAIL $1 ${2:-default}: got '$got', want '$3'"; fail=1;; esac
}
check http_literal  ""          "xisto: tls not linked"
check http_template ""          "xisto: tls not linked"
check https_literal "--tls=bearssl" "xisto: tls BearSSL"
check dynamic       "--tls=bearssl" "xisto: tls BearSSL"
check listen_cert   "--tls=bearssl" "xisto: tls BearSSL"
if [ -f build/awslc/lib/libssl.a ] || [ -n "${XISTO_AWSLC:-}" ]; then
    check https_literal "" "xisto: tls AWS-LC"
    check dynamic       "" "xisto: tls AWS-LC"
fi
check dynamic       "--tls=off" "xisto: tls not linked"
check https_literal "--tls=off" "error: https URL, but the program is built with --tls=off"
check listen_cert   "--tls=off" "error: HTTPS server, but the program is built with --tls=off"
$X build --tls=off -o $O/dyn $D/dynamic.ts && msg=$($O/dyn https://127.0.0.1:1 2>&1)
case "$msg" in *"https is not available: the program was built with --tls=off"*) echo "ok   runtime https under --tls=off";; *) echo "FAIL runtime: $msg"; fail=1;; esac
rm -rf $O
[ $fail = 0 ] && echo "tlsgate: all ok"
exit $fail
