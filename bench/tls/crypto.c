/* Micro-bench of the server-side handshake crypto, one operation at a time (µs per op, median of 7 runs).
   Build and run: sh bench/tls/crypto.sh */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "bearssl.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static int cmpd(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }
#define BENCH(label, N, body) do { double r_[7]; \
    for (int k_ = 0; k_ < 7; k_++) { double t0_ = now(); for (int i_ = 0; i_ < (N); i_++) { body; } r_[k_] = (now() - t0_) * 1e6 / (N); } \
    qsort(r_, 7, sizeof r_[0], cmpd); printf("%-34s %8.2f us\n", label, r_[3]); } while (0)

int main(void) {
    const br_ec_impl *ec = &br_ec_all_m31; /* what the TLS engine uses */
    br_hmac_drbg_context rng;
    br_hmac_drbg_init(&rng, &br_sha256_vtable, "xisto-bench", 11);
    unsigned char kbuf[BR_EC_KBUF_PRIV_MAX_SIZE], pbuf[BR_EC_KBUF_PUB_MAX_SIZE];
    br_ec_private_key sk; br_ec_public_key pk;
    br_ec_keygen(&rng.vtable, ec, &sk, kbuf, BR_EC_secp256r1);
    br_ec_compute_pub(ec, &pk, pbuf, &sk);
    unsigned char scalar[32], out[65], hash[32], sig[80];
    br_hmac_drbg_generate(&rng, scalar, 32); scalar[0] &= 0x7F;
    br_hmac_drbg_generate(&rng, hash, 32);
    size_t glen;
    const unsigned char *G = ec->generator(BR_EC_secp256r1, &glen);
    (void)G;

    BENCH("P-256 mulgen (k*G)", 2000, ec->mulgen(out, scalar, 32, BR_EC_secp256r1));
    unsigned char P[65]; memcpy(P, pk.q, 65);
    BENCH("P-256 mul (k*P)", 1000, { memcpy(P, pk.q, 65); ec->mul(P, 65, scalar, 32, BR_EC_secp256r1); });
    BENCH("ECDSA P-256 sign (i31)", 1000, br_ecdsa_i31_sign_raw(ec, &br_sha256_vtable, hash, &sk, sig));
    size_t siglen = br_ecdsa_i31_sign_raw(ec, &br_sha256_vtable, hash, &sk, sig);
    BENCH("ECDSA P-256 verify (i31)", 1000, br_ecdsa_i31_vrfy_raw(ec, hash, 32, &pk, sig, siglen));
    unsigned char xk[32], xo[32], xp[32];
    br_hmac_drbg_generate(&rng, xk, 32);
    BENCH("X25519 mulgen (keygen)", 2000, ec->mulgen(xo, xk, 32, BR_EC_curve25519));
    memcpy(xp, xo, 32);
    BENCH("X25519 mul (shared secret)", 2000, { memcpy(xo, xp, 32); ec->mul(xo, 32, xk, 32, BR_EC_curve25519); });
    unsigned char buf[1024] = {0}, h[32];
    BENCH("SHA-256 of 1 KB", 20000, { br_sha256_context c; br_sha256_init(&c); br_sha256_update(&c, buf, 1024); br_sha256_out(&c, h); });
    return 0;
}
