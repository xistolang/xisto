#!/bin/sh
# Handshake crypto micro-benches against build/libbearssl.a:
#   tls-crypto     : each operation alone (µs per op)
#   tls-handshake  : full in-memory handshakes, server CPU per handshake, ECDHE on P-256 and on X25519
cd "$(dirname "$0")/../.." || exit 1
O=bench/out; mkdir -p $O
openssl x509 -in tests/tls/cert.pem -outform DER -out $O/cert.der 2>/dev/null
openssl ec -in tests/tls/key.pem -outform DER -out $O/key.der 2>/dev/null
{ (cd $O && xxd -i cert.der | sed 's/unsigned char cert_der/static const unsigned char cert_der/; s/unsigned int/static const unsigned int/'); \
  (cd $O && xxd -i key.der | sed 's/unsigned char key_der/static const unsigned char key_der/; s/unsigned int/static const unsigned int/'); } > $O/tls_test_keys.h
cc -O2 -Ibuild/bearssl-inc -o $O/tls-crypto bench/tls/crypto.c build/libbearssl.a || exit 1
cc -O2 -Ibuild/bearssl-inc -I$O -o $O/tls-handshake bench/tls/handshake.c build/libbearssl.a || exit 1
$O/tls-crypto
for i in 1 2 3; do $O/tls-handshake p256 2000; done | sort -t' ' -k5 -n | sed -n 2p
for i in 1 2 3; do $O/tls-handshake x25519 2000; done | sort -t' ' -k5 -n | sed -n 2p
